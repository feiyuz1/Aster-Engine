#include "MiniEngine/Render/MaterialCompiler.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace MiniEngine
{
namespace
{
using Type = MaterialParameterType;
int Dimensions(Type type) { const int index = static_cast<int>(type); return index >= 0 && index < 4 ? index + 1 : 0; }
std::string GlslType(Type type) { return type == Type::Float ? "float" : "vec" + std::to_string(Dimensions(type)); }
std::string FloatLiteral(float value)
{
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    auto text = out.str();
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";
    return text;
}
std::string ConstantLiteral(Type type, Vec4 value)
{
    const float values[] = {value.x, value.y, value.z, value.w};
    if (type == Type::Float) return FloatLiteral(value.x);
    std::string text = GlslType(type) + '(';
    for (int i = 0; i < Dimensions(type); ++i) text += (i ? "," : "") + FloatLiteral(values[i]);
    return text + ')';
}
int InputCount(MaterialNodeType type)
{
    switch (type)
    {
    case MaterialNodeType::PbrOutput: case MaterialNodeType::SurfaceOutput: return 7;
    case MaterialNodeType::Add: case MaterialNodeType::Multiply: case MaterialNodeType::TextureSample: return 2;
    case MaterialNodeType::Lerp: case MaterialNodeType::Clamp: return 3;
    case MaterialNodeType::OneMinus: case MaterialNodeType::ComponentMask: case MaterialNodeType::NormalDecode: return 1;
    default: return 0;
    }
}
struct Expression { Type type = Type::Float; std::string text = "0.0"; bool valid = true; };
class Emitter
{
public:
    Emitter(const MaterialDefinitionData &data, const MaterialParameterLayout &layout, std::vector<MaterialDiagnostic> &diagnostics)
        : m_data(data), m_layout(layout), m_diagnostics(diagnostics)
    {
        for (const auto &node : data.graph.GetNodes()) m_nodes[node.id] = &node;
        for (const auto &link : data.graph.GetLinks()) m_inputs[{link.toNode, link.toInput}] = link.fromNode;
    }
    void Error(std::uint32_t id, int port, const std::string &message)
    { m_diagnostics.push_back({MaterialDiagnosticSeverity::Error, m_data.sourcePath, id, port, message}); }
    Expression Input(std::uint32_t id, std::uint32_t port, Expression fallback = {})
    {
        const auto input = m_inputs.find({id, port});
        return input == m_inputs.end() ? fallback : Emit(input->second);
    }
    Expression Convert(Expression value, Type expected, std::uint32_t id, int port)
    {
        if (!value.valid) return value;
        if (value.type == expected) return value;
        if (value.type == Type::Float && expected != Type::Texture2D)
        { value.text = GlslType(expected) + '(' + value.text + ')'; value.type = expected; return value; }
        Error(id, port, "Expected " + std::string(MaterialParameterTypeName(expected)) + ", got " + MaterialParameterTypeName(value.type));
        value.valid = false; return value;
    }
    Expression Emit(std::uint32_t id)
    {
        const auto cached = m_expressions.find(id);
        if (cached != m_expressions.end()) return cached->second;
        if (!m_visiting.insert(id).second) { Error(id, -1, "Reachable material graph cycle"); return {Type::Float, "0.0", false}; }
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) { Error(id, -1, "Missing graph node"); m_visiting.erase(id); return {Type::Float, "0.0", false}; }
        const auto &node = *found->second;
        Expression value;
        if (node.type == MaterialNodeType::Constant)
            value = {node.constantType, ConstantLiteral(node.constantType, node.value), true};
        else if (node.type == MaterialNodeType::ScalarParameter || node.type == MaterialNodeType::VectorParameter || node.type == MaterialNodeType::TextureParameter)
        {
            const auto &slots = node.type == MaterialNodeType::TextureParameter ? m_layout.textureSlots : m_layout.numericSlots;
            const auto parameter = std::find_if(slots.begin(), slots.end(), [&](const auto &p) { return p.name == node.parameterName; });
            if (parameter == slots.end()) { Error(id, -1, "Missing parameter declaration: " + node.parameterName); value.valid = false; }
            else if (parameter->type == Type::Texture2D) value = {Type::Texture2D, "te_texture_" + std::to_string(parameter->slot), true};
            else
            {
                static const char *swizzles[] = {".x", ".xy", ".xyz", ""};
                value = {parameter->type, "te_parameters[" + std::to_string(parameter->slot) + "]" + swizzles[Dimensions(parameter->type)-1], true};
            }
        }
        else if (node.type == MaterialNodeType::TexCoord) value = {Type::Vec2, "uv", true};
        else if (node.type == MaterialNodeType::TextureSample)
        {
            const auto texture = Input(id,0,{Type::Texture2D, "", false});
            const auto uv = Input(id,1,{Type::Vec2,"uv",true});
            if (!texture.valid || texture.type != Type::Texture2D || !uv.valid || uv.type != Type::Vec2)
            { Error(id, -1, "TextureSample requires Texture2D and Vec2 UV"); value.valid = false; }
            else value = {Type::Vec4, "texture(" + texture.text + ',' + uv.text + ')', true};
        }
        else if (node.type == MaterialNodeType::ComponentMask)
        {
            const auto input = Input(id,0,{Type::Vec4,"vec4(0.0)",true});
            bool valid = input.valid && Dimensions(input.type) > 0 && !node.channelMask.empty() && node.channelMask.size() <= 4;
            for (char c : node.channelMask)
            {
                const auto index = std::string("xyzw").find(c);
                valid = valid && index != std::string::npos && index < static_cast<std::size_t>(Dimensions(input.type));
            }
            if (!valid) { Error(id,0,"ComponentMask channels exceed the numeric input dimension"); value.valid = false; }
            else
            {
                value.type = static_cast<Type>(node.channelMask.size()-1);
                if (input.type == Type::Float) value.text = node.channelMask.size() == 1 ? input.text : GlslType(value.type) + '(' + input.text + ')';
                else value.text = '(' + input.text + ")." + node.channelMask;
            }
        }
        else if (node.type == MaterialNodeType::NormalDecode)
        {
            const auto input = Convert(Input(id,0,{Type::Vec3,"vec3(0.5,0.5,1.0)",true}),Type::Vec3,id,0);
            value = {Type::Vec3,"(" + input.text + " * 2.0 - 1.0)",input.valid};
        }
        else if (node.type == MaterialNodeType::OneMinus || node.type == MaterialNodeType::Clamp)
        {
            const auto input = Input(id,0);
            if (Dimensions(input.type) == 0) { Error(id,0,"Node requires a numeric input"); value.valid = false; }
            else if (node.type == MaterialNodeType::OneMinus) value = {input.type,"(1.0 - " + input.text + ')',input.valid};
            else
            {
                const auto minimum = Input(id,1), maximum = Input(id,2,{Type::Float,"1.0",true});
                if (minimum.type != Type::Float || maximum.type != Type::Float)
                { Error(id,-1,"Clamp minimum and maximum must be Float"); value.valid = false; }
                else value = {input.type,"clamp(" + input.text + ',' + minimum.text + ',' + maximum.text + ')',input.valid && minimum.valid && maximum.valid};
            }
        }
        else if (node.type == MaterialNodeType::Add || node.type == MaterialNodeType::Multiply || node.type == MaterialNodeType::Lerp)
        {
            auto a = Input(id,0), b = Input(id,1);
            if (!a.valid || !b.valid || Dimensions(a.type) == 0 || Dimensions(b.type) == 0 ||
                (a.type != b.type && a.type != Type::Float && b.type != Type::Float))
            { Error(id,-1,"Numeric inputs must have matching dimensions or Float broadcast"); value.valid = false; }
            else
            {
                const auto type = Dimensions(a.type) > Dimensions(b.type) ? a.type : b.type;
                a = Convert(a,type,id,0); b = Convert(b,type,id,1);
                value.type = type;
                if (node.type == MaterialNodeType::Lerp)
                {
                    const auto alpha = Input(id,2,{Type::Float,"0.5",true});
                    if (!alpha.valid || alpha.type != Type::Float) { Error(id,2,"Lerp alpha must be Float"); value.valid = false; }
                    else value.text = "mix(" + a.text + ',' + b.text + ',' + alpha.text + ')';
                }
                else value.text = '(' + a.text + (node.type == MaterialNodeType::Add ? " + " : " * ") + b.text + ')';
            }
        }
        else { Error(id,-1,"Node cannot produce a value"); value.valid = false; }
        m_visiting.erase(id);
        if (value.valid && value.type != Type::Texture2D)
        {
            const auto name = "te_n" + std::to_string(id);
            m_body << "    " << GlslType(value.type) << ' ' << name << " = " << value.text << ";\n";
            value.text = name;
        }
        m_expressions[id] = value;
        return value;
    }
    std::string Body() const { return m_body.str(); }
private:
    const MaterialDefinitionData &m_data;
    const MaterialParameterLayout &m_layout;
    std::vector<MaterialDiagnostic> &m_diagnostics;
    std::map<std::uint32_t,const MaterialNode *> m_nodes;
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> m_inputs;
    std::map<std::uint32_t,Expression> m_expressions;
    std::set<std::uint32_t> m_visiting;
    std::ostringstream m_body;
};
bool HasErrors(const std::vector<MaterialDiagnostic> &diagnostics)
{
    return std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto &d) { return d.severity == MaterialDiagnosticSeverity::Error; });
}
std::string BuiltinSurface(const MaterialParameterLayout &layout, bool opacity)
{
    const auto number = [&](const std::string &name) {
        const auto p = std::find_if(layout.numericSlots.begin(),layout.numericSlots.end(),[&](const auto &s) { return s.name == name; });
        return "te_parameters[" + std::to_string(p->slot) + ']';
    };
    const auto texture = [&](const std::string &name) {
        const auto p = std::find_if(layout.textureSlots.begin(),layout.textureSlots.end(),[&](const auto &s) { return s.name == name; });
        return "texture(te_texture_" + std::to_string(p->slot) + ",uv)";
    };
    const auto alpha = number("BaseColor") + ".w * " + texture("BaseMap") + ".a * " + number("Opacity") + ".x";
    if (opacity) return "float EvaluateOpacity(vec2 uv) { return clamp(" + alpha + ",0.0,1.0); }\n";
    std::ostringstream out;
    out << "SurfaceData EvaluateSurface(vec2 uv) {\n    SurfaceData s;\n"
        << "    vec3 te_packedChannels = " << number("PackedEnabled") << ".x > 0.5 ? " << texture("PackedMap") << ".rgb : vec3(1.0);\n"
        << "    s.BaseColor = " << number("BaseColor") << ".rgb * " << texture("BaseMap") << ".rgb;\n"
        << "    s.Metallic = clamp(" << number("Metallic") << ".x * (" << number("MetallicMapEnabled") << ".x > 0.5 ? " << texture("MetallicMap") << ".b : te_packedChannels.b),0.0,1.0);\n"
        << "    s.Roughness = clamp(" << number("Roughness") << ".x * (" << number("RoughnessMapEnabled") << ".x > 0.5 ? " << texture("RoughnessMap") << ".g : (" << number("PackedLayout") << ".x > 0.5 ? te_packedChannels.r : te_packedChannels.g)),0.04,1.0);\n"
        << "    s.AO = clamp(" << number("AO") << ".x * (" << number("AOMapEnabled") << ".x > 0.5 ? " << texture("AOMap") << ".r : (" << number("PackedLayout") << ".x > 0.5 ? te_packedChannels.g : te_packedChannels.r)),0.0,1.0);\n"
        << "    s.NormalTS = " << texture("NormalMap") << ".xyz * 2.0 - 1.0;\n"
        << "    s.Emissive = " << number("Emissive") << ".xyz * " << texture("EmissiveMap") << ".rgb;\n"
        << "    s.Opacity = clamp(" << alpha << ",0.0,1.0);\n    return s;\n}\n";
    return out.str();
}
}
MaterialCompileResult MaterialCompiler::Compile(const MaterialDefinitionData &definition) const
{
    MaterialCompileResult result;
    auto data = definition;
    if (data.graph.GetFormatVersion() == 1)
    {
        if (!data.graph.UpgradeLegacy()) { result.diagnostics.push_back({MaterialDiagnosticSeverity::Error,data.sourcePath,0,-1,"Invalid v1 output mapping"}); return result; }
        data.parameters = data.graph.GetParameters();
    }
    auto compiled = std::make_shared<CompiledMaterial>();
    compiled->renderState = data.renderState;
    compiled->dependencies.push_back(data.sourcePath);
    const auto error = [&](std::uint32_t node, int port, const std::string &message) {
        result.diagnostics.push_back({MaterialDiagnosticSeverity::Error,data.sourcePath,node,port,message});
    };
    if (data.graph.GetFormatVersion() != 2 || !std::isfinite(data.renderState.maskCutoff) || data.renderState.maskCutoff < 0 ||
        data.renderState.maskCutoff > 1 || static_cast<int>(data.renderState.shadingModel) < 0 || static_cast<int>(data.renderState.shadingModel) > 1 ||
        static_cast<int>(data.renderState.blendMode) < 0 || static_cast<int>(data.renderState.blendMode) > 2)
        error(0,-1,"Unsupported graph version or invalid render settings");
    auto parameters = data.parameters;
    std::sort(parameters.begin(),parameters.end(),[](const auto &a,const auto &b) { return a.name < b.name; });
    std::string previous;
    for (const auto &p : parameters)
    {
        if (!IsMaterialParameterName(p.name) || p.name == previous || p.type != GetMaterialParameterType(p.defaultValue) || !IsFiniteMaterialValue(p.defaultValue) ||
            (p.type != Type::Texture2D && p.sampling != TextureSampling::Linear) || static_cast<int>(p.sampling) < 0 || static_cast<int>(p.sampling) > 2)
        { error(0,-1,"Invalid, duplicate or mismatched parameter declaration: " + p.name); continue; }
        previous = p.name;
        auto &slots = p.type == Type::Texture2D ? compiled->layout.textureSlots : compiled->layout.numericSlots;
        slots.push_back({p.name,p.type,static_cast<std::uint32_t>(slots.size()),p.sampling});
        compiled->layout.canonicalSchema += p.name + ':' + MaterialParameterTypeName(p.type) + ':' + std::to_string(static_cast<int>(p.sampling)) + '\n';
    }
    if (compiled->layout.numericSlots.size() > 64 || compiled->layout.textureSlots.size() > 8) error(0,-1,"Material exceeds 64 numeric or 8 texture parameters");
    if (HasErrors(result.diagnostics)) return result;
    if (!compiled->layout.numericSlots.empty()) compiled->parameterSource = "layout(std140,binding=2) uniform MaterialParameters { vec4 te_parameters[64]; };\n";
    for (const auto &p : compiled->layout.textureSlots) compiled->parameterSource += "uniform sampler2D te_texture_" + std::to_string(p.slot) + ";\n";
    if (data.sourcePath == "builtin:PBR")
    {
        const auto required = MakeBuiltinPbrDefinition().parameters;
        for (const auto &p : required)
            if (std::none_of(parameters.begin(),parameters.end(),[&](const auto &q) { return q.name == p.name && q.type == p.type; })) error(0,-1,"Incomplete builtin PBR schema: " + p.name);
        if (HasErrors(result.diagnostics)) return result;
        compiled->surfaceSource = BuiltinSurface(compiled->layout,false);
        compiled->opacitySource = BuiltinSurface(compiled->layout,true);
    }
    else
    {
        std::map<std::uint32_t,const MaterialNode *> nodes;
        std::uint32_t output = 0;
        int outputCount = 0;
        for (const auto &node : data.graph.GetNodes())
        {
            if (node.id == 0 || !nodes.emplace(node.id,&node).second) error(node.id,-1,"Duplicate or zero node ID");
            if (static_cast<int>(node.type) < 0 || static_cast<int>(node.type) > 15) error(node.id,-1,"Unknown material node type");
            if (node.type == MaterialNodeType::SurfaceOutput || node.type == MaterialNodeType::PbrOutput) { output = node.id; ++outputCount; }
            if (node.type == MaterialNodeType::Constant && (Dimensions(node.constantType) == 0 || !IsFiniteMaterialValue(node.value))) error(node.id,-1,"Invalid numeric Constant");
            if (node.type == MaterialNodeType::TexCoord && node.uvIndex != 0) error(node.id,-1,"Only UV0 is supported");
            if (node.type == MaterialNodeType::ScalarParameter || node.type == MaterialNodeType::VectorParameter || node.type == MaterialNodeType::TextureParameter)
            {
                const auto p = std::find_if(parameters.begin(),parameters.end(),[&](const auto &q) { return q.name == node.parameterName; });
                if (p == parameters.end() || (node.type == MaterialNodeType::ScalarParameter && p->type != Type::Float) ||
                    (node.type == MaterialNodeType::TextureParameter && p->type != Type::Texture2D) ||
                    (node.type == MaterialNodeType::VectorParameter && (Dimensions(p->type) < 2))) error(node.id,-1,"Parameter node does not match its declaration");
            }
        }
        if (outputCount != 1) error(output,-1,"Material requires exactly one SurfaceOutput");
        std::set<std::pair<std::uint32_t,std::uint32_t>> inputs;
        for (const auto &link : data.graph.GetLinks())
        {
            const auto from = nodes.find(link.fromNode), to = nodes.find(link.toNode);
            if (from == nodes.end() || to == nodes.end() || link.fromOutput != 0 ||
                (from != nodes.end() && (from->second->type == MaterialNodeType::SurfaceOutput || from->second->type == MaterialNodeType::PbrOutput)) ||
                (to != nodes.end() && link.toInput >= static_cast<std::uint32_t>(InputCount(to->second->type))) || !inputs.insert({link.toNode,link.toInput}).second)
                error(link.toNode,static_cast<int>(link.toInput),"Missing node, invalid port or duplicate input link");
        }
        if (HasErrors(result.diagnostics)) return result;
        std::set<std::uint32_t> reachable;
        std::function<void(std::uint32_t)> mark = [&](auto id) {
            if (!reachable.insert(id).second) return;
            for (const auto &link : data.graph.GetLinks()) if (link.toNode == id) mark(link.fromNode);
        };
        mark(output);
        std::map<std::uint32_t,int> status;
        bool isolatedCycle = false;
        std::function<void(std::uint32_t)> visit = [&](auto id) {
            if (reachable.count(id) || status[id] == 2) return;
            if (status[id] == 1) { isolatedCycle = true; return; }
            status[id] = 1;
            for (const auto &link : data.graph.GetLinks()) if (link.toNode == id) visit(link.fromNode);
            status[id] = 2;
        };
        for (const auto &node : nodes) visit(node.first);
        if (isolatedCycle) result.diagnostics.push_back({MaterialDiagnosticSeverity::Warning,data.sourcePath,0,-1,"Unreachable cycle ignored"});
        Emitter surface(data,compiled->layout,result.diagnostics);
        static const Type types[] = {Type::Vec3,Type::Float,Type::Float,Type::Vec3,Type::Float,Type::Vec3,Type::Float};
        static const char *names[] = {"BaseColor","Metallic","Roughness","NormalTS","AO","Emissive","Opacity"};
        static const char *defaults[] = {"vec3(0.8)","0.0","0.7","vec3(0.0,0.0,1.0)","1.0","vec3(0.0)","1.0"};
        std::string assignments;
        for (std::uint32_t i=0;i<7;++i)
        {
            const auto value = surface.Convert(surface.Input(output,i,{types[i],defaults[i],true}),types[i],output,static_cast<int>(i));
            assignments += "    s." + std::string(names[i]) + " = " + value.text + ";\n";
        }
        compiled->surfaceSource = "SurfaceData EvaluateSurface(vec2 uv) {\n" + surface.Body() + "    SurfaceData s;\n" + assignments +
            "    s.Metallic = clamp(s.Metallic,0.0,1.0); s.Roughness = clamp(s.Roughness,0.04,1.0);\n"
            "    s.AO = clamp(s.AO,0.0,1.0); s.Opacity = clamp(s.Opacity,0.0,1.0);\n    return s;\n}\n";
        Emitter opacity(data,compiled->layout,result.diagnostics);
        const auto alpha = opacity.Convert(opacity.Input(output,6,{Type::Float,"1.0",true}),Type::Float,output,6);
        compiled->opacitySource = "float EvaluateOpacity(vec2 uv) {\n" + opacity.Body() + "    return clamp(" + alpha.text + ",0.0,1.0);\n}\n";
    }
    if (HasErrors(result.diagnostics)) return result;
    compiled->canonicalProgramContent = "MaterialCompiler:1\n" + compiled->layout.canonicalSchema + compiled->parameterSource + compiled->surfaceSource +
        compiled->opacitySource + "state:" + std::to_string(static_cast<int>(data.renderState.shadingModel)) + ':' + std::to_string(static_cast<int>(data.renderState.blendMode));
    result.material = std::move(compiled);
    return result;
}
GeneratedShaderSources MaterialCompiler::GeneratePass(const CompiledMaterial &material, MaterialPass pass, MaterialGeometry geometry,
                                                     const MaterialTemplateSources &templates) const
{
    GeneratedShaderSources generated;
    std::set<std::string> stack, dependencies;
    std::function<bool(const std::string &,std::string &)> expand = [&](const std::string &key,std::string &output) {
        const auto found = templates.files.find(key);
        if (found == templates.files.end()) { generated.error = "Missing material shader template: " + key; return false; }
        if (!stack.insert(key).second) { generated.error = "Cyclic material template include: " + key; return false; }
        dependencies.insert(key);
        std::istringstream input(found->second);
        std::string line;
        while (std::getline(input,line))
        {
            const auto include = line.find("#include");
            if (include != std::string::npos)
            {
                const auto first = line.find('"',include), last = first == std::string::npos ? first : line.find('"',first+1);
                if (first == std::string::npos || last == std::string::npos) { generated.error = "Malformed material template include: " + key; return false; }
                const auto child = (std::filesystem::u8path(key).parent_path()/std::filesystem::u8path(line.substr(first+1,last-first-1))).lexically_normal().generic_u8string();
                if (!expand(child,output)) return false;
            }
            else if (line.find("/*MATERIAL_DECLARATIONS*/") != std::string::npos) output += material.parameterSource;
            else if (line.find("/*MATERIAL_FUNCTIONS*/") != std::string::npos) output += pass == MaterialPass::Base ? material.surfaceSource + material.opacitySource : material.opacitySource;
            else
            {
                output += line + '\n';
                if (line.rfind("#version",0) == 0)
                    output += "#define TE_GEOMETRY " + std::to_string(static_cast<int>(geometry)) + "\n#define TE_BLEND " +
                        std::to_string(static_cast<int>(material.renderState.blendMode)) + "\n#define TE_UNLIT " +
                        std::to_string(material.renderState.shadingModel == MaterialShadingModel::Unlit ? 1 : 0) + '\n';
            }
        }
        stack.erase(key);
        return true;
    };
    const std::string base = pass == MaterialPass::Base ? "BasePass" : "ShadowPass";
    if (!expand(base+".vert",generated.vertex) || !expand(base+".frag",generated.fragment)) { generated.vertex.clear(); generated.fragment.clear(); }
    generated.dependencies.assign(dependencies.begin(),dependencies.end());
    return generated;
}
bool MaterialCompiler::WriteCache(const CompiledMaterial &material,const MaterialTemplateSources &templates,const std::filesystem::path &directory,std::string *cacheKey) const
{
    std::map<std::string,std::string> files; std::set<std::string> dependencies;
    std::string content=material.canonicalProgramContent;
    for (auto pass:{MaterialPass::Base,MaterialPass::Shadow}) for (auto geometry:{MaterialGeometry::Static,MaterialGeometry::Skinned,MaterialGeometry::Instanced})
    {
        const auto generated=GeneratePass(material,pass,geometry,templates); if (!generated.error.empty()) return false;
        const std::string name=std::string(pass==MaterialPass::Base ? "Base" : "Shadow")+'-'+(geometry==MaterialGeometry::Static ? "Static" : geometry==MaterialGeometry::Skinned ? "Skinned" : "Instanced");
        files[name+".vert"]=generated.vertex; files[name+".frag"]=generated.fragment;
        content+=name+'\n'+generated.vertex+generated.fragment; dependencies.insert(generated.dependencies.begin(),generated.dependencies.end());
    }
    std::uint64_t hash=1469598103934665603ull; for (unsigned char c:content) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream hex; hex << std::hex << hash; std::string key=hex.str();
    for (unsigned collision=0;;++collision)
    {
        key=hex.str()+(collision ? "-"+std::to_string(collision) : "");
        const auto previous=PlatformFile::ReadAllText(directory/key/"content.key");
        if (previous.empty() || previous==content) break;
    }
    const auto root=directory/key;
    const auto writeChanged=[](const std::filesystem::path &path,const std::string &source) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path,error) && !error && PlatformFile::ReadAllText(path)==source) return true;
        return PlatformFile::WriteAllTextAtomic(path,source);
    };
    if (!writeChanged(root/"content.key",content)) return false;
    for (const auto &file:files) if (!writeChanged(root/file.first,file.second)) return false;
    std::string layout=material.layout.canonicalSchema;
    for (const auto &slot:material.layout.numericSlots) layout+=slot.name+" numeric-slot "+std::to_string(slot.slot)+'\n';
    for (const auto &slot:material.layout.textureSlots) layout+=slot.name+" texture-slot "+std::to_string(slot.slot)+'\n';
    dependencies.insert(material.dependencies.begin(),material.dependencies.end()); std::string summary;
    for (const auto &dependency:dependencies) summary+=dependency+'\n';
    if (!writeChanged(root/"layout.txt",layout) || !writeChanged(root/"dependencies.txt",summary)) return false;
    if (cacheKey) *cacheKey=key; return true;
}
} // namespace MiniEngine
