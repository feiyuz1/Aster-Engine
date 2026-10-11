#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/MaterialSystem.hpp"
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <limits>

using namespace MiniEngine;
namespace
{
int failures = 0;
void Check(bool condition, const char* label)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
    if (!condition) ++failures;
}
bool Near(float a, float b) { return std::abs(a - b) < 0.00001f; }
const std::string root = "Saved/MaterialTests/";
void Write(const std::string& name, const std::string& text)
{
    Check(PlatformFile::WriteAllText(std::filesystem::u8path(root + name), text), "write material fixture");
}
void TestLegacyMaterialCompatibility()
{
    Write("parent.mat", "color .9 .2 .1 .8\nmetallic .8\nroughness .4\ntexture white.ppm\npackedTexture channels.ppm\npackedLayout RMA\nblend alpha\n");
    Write("child.mat", "parent parent.mat\noverride color 0\ncolor .1 .1 .1 1\nroughness .6\n");
    MaterialResource child;
    Check(child.Load(root + "child.mat"), "legacy parent loads");
    Check(Near(child.GetBaseColor().x, .9f) && Near(child.GetMetallic(), .8f) && Near(child.GetRoughness(), .6f),
          "legacy false override inherits; missing override applies");
    Check(child.GetTexturePath() == "white.ppm" && child.GetPackedTexturePath() == "channels.ppm" &&
          child.GetPackedTextureLayout() == "RMA" && child.IsTransparent(), "legacy texture, packed layout and blend survive");
}
void TestInvalidMaterialCandidates()
{
    Write("valid.mat", "color .91 .32 .14 1\nmetallic .75\n");
    MaterialResource material;
    Check(material.Load(root + "valid.mat"), "load valid material before failed replacement");
    Check(!material.Load(root + "missing.mat"), "missing material fails");
    Check(material.IsReady() && Near(material.GetBaseColor().x, .91f) && Near(material.GetMetallic(), .75f),
          "failed replacement retains previous ready material");
    Write("version.mat", "TINY_MAT 99\ndefinition \"builtin:PBR\"\n");
    Check(!material.Load(root + "version.mat"), "unknown material version is rejected");
    Write("duplicate.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"Metallic\" Float .1\nparameter \"Metallic\" Float .2\n");
    Check(!material.Load(root + "duplicate.mat"), "duplicate typed overrides are rejected");
    Write("ambiguous.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparent \"parent.mat\"\n");
    Check(!material.Load(root + "ambiguous.mat"), "definition and parent cannot coexist");
}
void TestTypedMaterialRoundTrip()
{
    Write("typed.mat", "TINY_MAT 2\ndefinition \"assets/材质 库/Parent.matgraph\"\n"
          "parameter \"Weight\" Float .25\nparameter \"Scale\" Vec2 2 3\nparameter \"Emission\" Vec3 4 5 6\n"
          "parameter \"Tint\" Vec4 .1 .2 .3 .4\ntexture \"BaseMap\" \"assets/贴图 库/图片.png\"\n");
    MaterialResource material;
    Check(material.Load(root + "typed.mat"), "typed material loads without requiring definition yet");
    const auto& data = material.GetMaterialData();
    Check(data.formatVersion == 2 && data.definitionPath == "assets/材质 库/Parent.matgraph" && data.overrides.size() == 5,
          "typed asset keeps quoted paths and overrides");
    Check(std::holds_alternative<float>(data.overrides.at("Weight")) &&
          std::holds_alternative<Vec2>(data.overrides.at("Scale")) &&
          Near(std::get<Vec2>(data.overrides.at("Scale")).y, 3) &&
          Near(std::get<Vec3>(data.overrides.at("Emission")).z, 6) &&
          Near(std::get<Vec4>(data.overrides.at("Tint")).w, .4f) &&
          std::get<std::string>(data.overrides.at("BaseMap")) == "assets/贴图 库/图片.png", "five distinct parameter types parsed");
    Check(material.Save(root + "roundtrip.mat"), "save typed instance");
    MaterialResource reopened;
    Check(reopened.Load(root + "roundtrip.mat") && reopened.GetMaterialData().definitionPath == data.definitionPath &&
          Near(std::get<Vec4>(reopened.GetMaterialData().overrides.at("Tint")).z, .3f) &&
          std::get<std::string>(reopened.GetMaterialData().overrides.at("BaseMap")) == std::get<std::string>(data.overrides.at("BaseMap")),
          "typed values and unicode paths survive save and reopen");
    Write("malformed.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"te_internal\" Float .4\n");
    Check(!reopened.Load(root + "malformed.mat"), "reserved parameter prefix rejected");
    Write("truncated.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"Tint\" Vec4 1 2\n");
    Check(!reopened.Load(root + "truncated.mat"), "truncated typed value rejected");
}
const MaterialParameterValue& Value(const ResolvedMaterial& material, const std::string& name)
{
    const auto& declarations = material.definition->parameters;
    for (std::size_t i = 0; i < declarations.size(); ++i) if (declarations[i].name == name) return material.values.at(i);
    throw std::runtime_error("Missing resolved parameter " + name);
}
void WriteDefinition()
{
    Write("assets/材质 库/paint.matgraph", "TINY_MATGRAPH 2\nsettings Lit Opaque 0 .5\n"
        "parameter \"Tint\" Vec3 Linear .8 .7 .6\nparameter_meta \"Tint\" \"颜色\" \"外观\" 0 0 1\n"
        "parameter \"Roughness\" Float Linear .7\nparameter \"BaseMap\" Texture2D SRgb \"../贴图 库/base.ppm\"\n"
        "node 1 SurfaceOutput \"输出\" 100 0 Float 0 0 0 0 \"\" \"\"\n");
    Write("assets/贴图 库/base.ppm", "P3\n1 1\n255\n255 255 255\n");
}
void TestThreeLevelInheritance()
{
    WriteDefinition();
    Write("assets/parent.mat", "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Tint\" Vec3 1 .1 .2\n");
    Write("assets/middle.mat", "TINY_MAT 2\nparent \"./parent.mat\"\nparameter \"Roughness\" Float .3\n");
    Write("assets/child.mat", "TINY_MAT 2\nparent \"middle.mat\"\nparameter \"Tint\" Vec3 .1 .2 1\n");
    ResourceManager resources;
    resources.Initialize(nullptr, root + "assets");
    MaterialSystem system(resources);
    auto child = system.Resolve("assets/child.mat");
    Check(child && Near(std::get<Vec3>(Value(*child, "Tint")).z, 1) &&
          Near(std::get<float>(Value(*child, "Roughness")), .3f), "three level child to parent to definition precedence");
    Write("assets/reset.mat", "TINY_MAT 2\nparent \"middle.mat\"\n");
    auto reset = system.Resolve("assets/reset.mat");
    Check(reset && Near(std::get<Vec3>(Value(*reset, "Tint")).x, 1), "absent child override restores inherited value");
    Write("assets/unknown.mat", "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Unknown\" Float 1\n");
    Check(!system.Resolve("assets/unknown.mat"), "first load rejects unknown parameter");
    Write("assets/wrong.mat", "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Tint\" Vec4 1 1 1 1\n");
    Check(!system.Resolve("assets/wrong.mat"), "first load rejects wrong override type");
    auto legacy = system.Resolve(std::filesystem::absolute(root + "child.mat").generic_string());
    Check(legacy && Near(std::get<Vec4>(Value(*legacy, "BaseColor")).x, .9f), "legacy adapter keeps false override inheritance");
}
void TestParentCyclesAndDepth()
{
    ResourceManager resources;
    resources.Initialize(nullptr, root + "assets");
    MaterialSystem system(resources);
    Write("assets/a.mat", "TINY_MAT 2\nparent \"./b.mat\"\n");
    Write("assets/b.mat", "TINY_MAT 2\nparent \"../assets/a.mat\"\n");
    Check(!system.Resolve("assets/a.mat"), "normalized parent cycle rejected");
    const auto& diagnostics = system.GetDiagnostics("assets/a.mat");
    Check(!diagnostics.empty() && diagnostics.front().message.find("a.mat") != std::string::npos &&
          diagnostics.front().message.find("b.mat") != std::string::npos, "cycle diagnostic contains complete chain");
    for (int i = 0; i < 33; ++i)
        Write("assets/depth" + std::to_string(i) + ".mat", "TINY_MAT 2\n" +
              (i == 0 ? std::string("definition \"builtin:PBR\"\n") : "parent \"depth" + std::to_string(i-1) + ".mat\"\n"));
    Check(static_cast<bool>(system.Resolve("assets/depth31.mat")), "32 instance nodes accepted");
    Check(!system.Resolve("assets/depth32.mat"), "33 instance nodes rejected even with cached parent");
    Write("assets/legacy0.mat", "color .9 .2 .1 1\n");
    for (int i = 1; i < 32; ++i) Write("assets/legacy" + std::to_string(i) + ".mat", "parent legacy" + std::to_string(i-1) + ".mat\n");
    Write("assets/mixedDepth.mat", "TINY_MAT 2\nparent \"legacy31.mat\"\n");
    Check(!system.Resolve("assets/mixedDepth.mat"), "depth budget includes legacy nodes beneath a typed instance");
}
void TestAssetRootPaths()
{
    const auto previous = std::filesystem::current_path();
    ResourceManager resources;
    resources.Initialize(nullptr, root + "assets");
    std::filesystem::current_path(previous / "build");
    MaterialSystem system(resources);
    auto material = system.Resolve("assets/child.mat");
    Check(material && PlatformFile::Exists(std::filesystem::u8path(std::get<std::string>(Value(*material, "BaseMap")))),
          "asset root and relative unicode texture survive CWD change");
    std::filesystem::current_path(previous);
}
void TestMaterialDependencies()
{
    const auto assetRoot = std::filesystem::absolute(root + "assets");
    AssetRegistry registry;
    registry.Scan(assetRoot, false);
    const auto parentPath = (assetRoot / "parent.mat").generic_string();
    const auto graphPath = (assetRoot / std::filesystem::u8path("材质 库/paint.matgraph")).generic_u8string();
    const auto childPath = (assetRoot / "child.mat").generic_string();
    auto parent = registry.Find(parentPath);
    auto graph = registry.Find(graphPath);
    Check(parent && std::find(parent->dependencies.begin(), parent->dependencies.end(), graphPath) != parent->dependencies.end(),
          "material indexes definition dependency");
    Check(graph && graph->dependencies.size() == 1 && PlatformFile::Exists(std::filesystem::u8path(graph->dependencies[0])), "graph indexes default texture");
    const auto dependents = registry.GetDependents((assetRoot / "middle.mat").generic_string());
    Check(std::find(dependents.begin(), dependents.end(), childPath) != dependents.end(), "reverse dependency index finds child instance");
    const auto moved = assetRoot / std::filesystem::u8path("材质 库/moved.matgraph");
    if (std::filesystem::exists(moved)) std::filesystem::remove(moved);
    auto movedMeta = moved; movedMeta += ".meta";
    if (std::filesystem::exists(movedMeta)) std::filesystem::remove(movedMeta);
    std::string error;
    Check(AssetMaintenance::MoveAsset(registry, std::filesystem::u8path(graphPath), moved, &error), "move definition asset");
    MaterialResource reopened;
    Check(reopened.Load(parentPath) && reopened.GetMaterialData().definitionPath.find("moved.matgraph") != std::string::npos,
          "asset move rewrites structured logical definition reference");
}
MaterialDefinitionData GraphDefinition()
{
    MaterialDefinitionData data;
    data.sourcePath = "test:graph";
    data.graph.SetFormatVersion(2);
    data.graph.AddNode(MaterialNodeType::SurfaceOutput, "Surface");
    return data;
}
std::uint32_t Constant(MaterialDefinitionData& data, MaterialParameterType type, Vec4 value)
{
    const auto id = data.graph.AddNode(MaterialNodeType::Constant, "Constant");
    auto& node = data.graph.GetNodes().back(); node.constantType = type; node.value = value;
    return id;
}
MaterialParameterDecl Declaration(const std::string& name, MaterialParameterValue value)
{
    MaterialParameterDecl p; p.name = name; p.type = GetMaterialParameterType(value); p.defaultValue = std::move(value); return p;
}
std::uint32_t Parameter(MaterialDefinitionData& data, const std::string& name, MaterialParameterValue value)
{
    const auto type = GetMaterialParameterType(value);
    data.parameters.push_back(Declaration(name, std::move(value)));
    const auto nodeType = type == MaterialParameterType::Texture2D ? MaterialNodeType::TextureParameter :
        type == MaterialParameterType::Float ? MaterialNodeType::ScalarParameter : MaterialNodeType::VectorParameter;
    const auto id = data.graph.AddNode(nodeType, name); data.graph.GetNodes().back().parameterName = name; return id;
}
std::uint32_t Mask(MaterialDefinitionData& data, std::uint32_t from, const std::string& channels)
{
    const auto id = data.graph.AddNode(MaterialNodeType::ComponentMask, "Mask");
    data.graph.GetNodes().back().channelMask = channels; data.graph.Connect(from, id, 0); return id;
}
void TestGraphTypes()
{
    MaterialCompiler compiler;
    auto graph = GraphDefinition();
    const auto vector = Constant(graph, MaterialParameterType::Vec4, {1, .3f, .2f, .7f});
    graph.graph.Connect(vector, 1, 0);
    Check(!compiler.Compile(graph).Success(), "Vec4 to BaseColor requires explicit component mask");
    graph.graph.Connect(Mask(graph, vector, "xyz"), 1, 0);
    Check(compiler.Compile(graph).Success(), "component mask provides typed BaseColor");
    auto numeric = GraphDefinition();
    const auto v = Constant(numeric, MaterialParameterType::Vec3, {.2f,.4f,.6f,0});
    const auto scalar = Parameter(numeric, "Scale", .5f);
    const auto multiply = numeric.graph.AddNode(MaterialNodeType::Multiply, "Multiply");
    numeric.graph.Connect(v, multiply, 0); numeric.graph.Connect(scalar, multiply, 1);
    const auto add = numeric.graph.AddNode(MaterialNodeType::Add, "Add");
    numeric.graph.Connect(multiply, add, 0); numeric.graph.Connect(scalar, add, 1); numeric.graph.Connect(add, 1, 0);
    Check(compiler.Compile(numeric).Success(), "Add and Multiply broadcast Float to Vec3");
    const auto oneMinus = numeric.graph.AddNode(MaterialNodeType::OneMinus, "OneMinus");
    numeric.graph.Connect(add, oneMinus, 0);
    const auto clamp = numeric.graph.AddNode(MaterialNodeType::Clamp, "Clamp");
    numeric.graph.Connect(oneMinus, clamp, 0);
    const auto lerp = numeric.graph.AddNode(MaterialNodeType::Lerp, "Lerp");
    numeric.graph.Connect(clamp, lerp, 0); numeric.graph.Connect(scalar, lerp, 1); numeric.graph.Connect(lerp, 1, 0);
    Check(compiler.Compile(numeric).Success(), "OneMinus, Clamp and Lerp support typed values and defaults");
    const auto bad = Constant(numeric, MaterialParameterType::Vec2, {1,2,0,0});
    numeric.graph.Connect(bad, lerp, 2);
    Check(!compiler.Compile(numeric).Success(), "Lerp alpha must be Float");
    numeric.graph.Connect(scalar, lerp, 2); numeric.graph.Connect(bad, clamp, 1);
    Check(!compiler.Compile(numeric).Success(), "Clamp minimum must be Float");
    auto sampled = GraphDefinition();
    const auto texture = Parameter(sampled, "Normal", std::string("normal.ppm"));
    sampled.parameters.back().sampling = TextureSampling::Normal;
    const auto uv = sampled.graph.AddNode(MaterialNodeType::TexCoord, "UV0");
    const auto sample = sampled.graph.AddNode(MaterialNodeType::TextureSample, "Sample");
    sampled.graph.Connect(texture, sample, 0); sampled.graph.Connect(uv, sample, 1);
    const auto normal = sampled.graph.AddNode(MaterialNodeType::NormalDecode, "NormalDecode");
    sampled.graph.Connect(Mask(sampled, sample, "xyz"), normal, 0); sampled.graph.Connect(normal, 1, 3);
    Check(compiler.Compile(sampled).Success(), "TextureParameter, TextureSample, UV0 and NormalDecode compile");
    auto badUv = sampled;
    const auto floatUv = Constant(badUv, MaterialParameterType::Float, {1,0,0,0});
    badUv.graph.Connect(floatUv, sample, 1);
    Check(!compiler.Compile(badUv).Success(), "texture UV requires Vec2");
    auto absent = GraphDefinition();
    const auto missing = absent.graph.AddNode(MaterialNodeType::TextureSample, "Missing");
    absent.graph.Connect(Mask(absent, missing, "xyz"), 1, 0);
    Check(!compiler.Compile(absent).Success(), "texture sample requires texture input");
    auto vectorParam = GraphDefinition();
    vectorParam.graph.Connect(Parameter(vectorParam, "Color", Vec3{1,.2f,.3f}), 1, 0);
    Check(compiler.Compile(vectorParam).Success(), "VectorParameter resolves declared vector type");
    auto invalidMask = graph; invalidMask.graph.GetNodes().back().channelMask = "xa";
    Check(!compiler.Compile(invalidMask).Success(), "invalid component mask is rejected");
    auto invalidUV = sampled;
    for (auto& node : invalidUV.graph.GetNodes()) if (node.id == uv) node.uvIndex = 1;
    Check(!compiler.Compile(invalidUV).Success(), "only UV0 is supported");
    auto duplicate = graph; duplicate.graph.GetNodes().push_back(duplicate.graph.GetNodes().back());
    Check(!compiler.Compile(duplicate).Success(), "duplicate node IDs rejected");
    auto port = graph; port.graph.GetLinks().front().fromOutput = 1;
    Check(!compiler.Compile(port).Success(), "unknown output port rejected");
    auto input = graph; input.graph.GetLinks().push_back(input.graph.GetLinks().front());
    Check(!compiler.Compile(input).Success(), "duplicate input links rejected");
}
void TestLegacyGraphUpgrade()
{
    Write("legacy.matgraph", "TINY_MATGRAPH 1\nnode 1 0 \"Color\" 0 0 .2 .4 .6 .8 \"\"\n"
        "node 2 4 \"Output\" 100 0 1 1 1 1 \"\"\nlink 1 2 0\nlink 1 2 1\nlink 1 2 2\nlink 1 2 3\n");
    MaterialDefinitionResource resource;
    Check(resource.Load(root + "legacy.matgraph"), "load v1 graph definition");
    const auto result = MaterialCompiler{}.Compile(resource.GetData());
    Check(result.Success() && result.material->surfaceSource.find(".xyz") != std::string::npos &&
          result.material->opacitySource.find(".w") != std::string::npos, "legacy RGB and alpha are explicitly mapped");
    auto upgraded = resource.GetData().graph;
    Check(upgraded.UpgradeLegacy() && upgraded.GetFormatVersion() == 2 && upgraded.Save(root + "upgraded.matgraph"), "explicit legacy graph save uses v2");
    MaterialDefinitionResource reopened;
    Check(reopened.Load(root + "upgraded.matgraph") && MaterialCompiler{}.Compile(reopened.GetData()).Success(), "upgraded graph reopens and compiles");
    MaterialDefinitionData emptyLegacy;
    const auto add = emptyLegacy.graph.AddNode(MaterialNodeType::Add,"Unconnected Add");
    const auto output = emptyLegacy.graph.AddNode(MaterialNodeType::PbrOutput,"Output");
    emptyLegacy.graph.Connect(add,output,0);
    Check(MaterialCompiler{}.Compile(emptyLegacy).Success(), "legacy unconnected arithmetic retains Vec4 defaults");
    Write("legacyTexture.matgraph", "TINY_MATGRAPH 1\nnode 1 1 \"Map\" 0 0 1 1 1 1 \"assets/贴图 库/base.ppm\"\n"
        "node 2 4 \"Output\" 100 0 1 1 1 1 \"\"\nlink 1 2 0\n");
    MaterialDefinitionResource textureDefinition;
    Check(textureDefinition.Load(root+"legacyTexture.matgraph") && textureDefinition.GetData().parameters.size() == 1 &&
          textureDefinition.GetData().parameters[0].type == MaterialParameterType::Texture2D,
          "legacy texture nodes expose declarations to the inheritance resolver");
}
void TestReachableCycles()
{
    auto graph = GraphDefinition();
    const auto a = graph.graph.AddNode(MaterialNodeType::Add, "A"), b = graph.graph.AddNode(MaterialNodeType::Add, "B");
    graph.graph.Connect(a,b,0); graph.graph.Connect(b,a,0); graph.graph.Connect(a,1,2);
    Check(!MaterialCompiler{}.Compile(graph).Success(), "reachable cycle fails compilation");
    graph.graph.GetLinks().pop_back();
    const auto result = MaterialCompiler{}.Compile(graph);
    Check(result.Success() && std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& d) {
        return d.severity == MaterialDiagnosticSeverity::Warning;
    }), "unreachable cycle produces warning and no shader code");
}
void TestDeterministicCompilation()
{
    auto graph = GraphDefinition();
    const auto p = Parameter(graph, "Rough", .25f); graph.graph.Connect(p,1,2);
    const auto c = Constant(graph, MaterialParameterType::Vec3, {.1f,.2f,.3f,0}); graph.graph.Connect(c,1,0);
    auto first = MaterialCompiler{}.Compile(graph);
    auto edited = graph; edited.parameters[0].defaultValue = .8f; edited.parameters[0].displayName = "中文显示名";
    edited.graph.GetNodes()[1].position = {500,300}; edited.renderState.twoSided = true; edited.renderState.maskCutoff = .8f;
    auto second = MaterialCompiler{}.Compile(edited);
    Check(first.Success() && second.Success() && first.material->canonicalProgramContent == second.material->canonicalProgramContent,
          "layout and program ignore parameter defaults, editor position and raster uniforms");
    edited.graph.GetNodes().back().value.x = .7f;
    auto changed = MaterialCompiler{}.Compile(edited);
    Check(changed.Success() && changed.material->canonicalProgramContent != first.material->canonicalProgramContent, "literal constant participates in program identity");
    auto sorted = GraphDefinition();
    sorted.parameters = {Declaration("Z", 1.f), Declaration("A", Vec2{2,3})};
    auto ordered = MaterialCompiler{}.Compile(sorted); std::reverse(sorted.parameters.begin(), sorted.parameters.end());
    auto reversed = MaterialCompiler{}.Compile(sorted);
    Check(ordered.Success() && reversed.Success() && ordered.material->layout.numericSlots[0].name == "A" &&
          ordered.material->canonicalProgramContent == reversed.material->canonicalProgramContent, "declaration ordering produces stable layout");
}
void TestLimits()
{
    auto graph = GraphDefinition();
    for (int i=0;i<64;++i) graph.parameters.push_back(Declaration("P"+std::to_string(i), 0.f));
    for (int i=0;i<8;++i) graph.parameters.push_back(Declaration("T"+std::to_string(i), std::string{}));
    Check(MaterialCompiler{}.Compile(graph).Success(), "64 numeric plus 8 texture declarations accepted");
    auto excess = graph; excess.parameters.push_back(Declaration("ExtraNumber", 0.f));
    Check(!MaterialCompiler{}.Compile(excess).Success(), "65th numeric declaration rejected");
    excess = graph; excess.parameters.push_back(Declaration("ExtraTexture", std::string{}));
    Check(!MaterialCompiler{}.Compile(excess).Success(), "9th texture declaration rejected");
    excess = graph; excess.parameters.push_back(excess.parameters[0]);
    Check(!MaterialCompiler{}.Compile(excess).Success(), "duplicate parameter declaration rejected");
    excess = GraphDefinition(); excess.parameters.push_back(Declaration("te_reserved", 0.f));
    Check(!MaterialCompiler{}.Compile(excess).Success(), "reserved parameter declaration rejected");
    excess = GraphDefinition(); excess.parameters.push_back(Declaration("Number", 0.f)); excess.parameters[0].sampling = TextureSampling::Normal;
    Check(!MaterialCompiler{}.Compile(excess).Success(), "numeric parameter sampling must be Linear");
    excess = GraphDefinition(); excess.renderState.maskCutoff = std::numeric_limits<float>::quiet_NaN();
    Check(!MaterialCompiler{}.Compile(excess).Success(), "non-finite render settings rejected before GPU upload");
}
void TestOpacityDependency()
{
    auto graph = GraphDefinition();
    graph.renderState.blendMode = MaterialBlendMode::Masked;
    const auto texture = Parameter(graph,"EmissionMap",std::string("emissive.ppm"));
    const auto sample = graph.graph.AddNode(MaterialNodeType::TextureSample,"Emission Sample"); graph.graph.Connect(texture,sample,0);
    graph.graph.Connect(Mask(graph,sample,"xyz"),1,5);
    graph.graph.Connect(Parameter(graph,"Alpha",.8f),1,6);
    const auto result = MaterialCompiler{}.Compile(graph);
    Check(result.Success() && result.material->surfaceSource.find("texture(") != std::string::npos &&
          result.material->opacitySource.find("texture(") == std::string::npos,
          "opacity compilation prunes emission-only texture sampling");
    MaterialTemplateSources templates;
    templates.files = {{"BasePass.vert","#version 430 core\n#include \"Transform.glsl\"\n"},
        {"Transform.glsl","void main() { gl_Position=vec4(0.0); }\n"}, {"Surface.glsl","struct SurfaceData { vec3 BaseColor; };\n"},
        {"ShadowPass.vert","#version 430 core\n#include \"Transform.glsl\"\n"},
        {"ShadowPass.frag","#version 430 core\n/*MATERIAL_DECLARATIONS*/\n/*MATERIAL_FUNCTIONS*/\nvoid main() {}\n"}};
    const auto shadow = MaterialCompiler{}.GeneratePass(*result.material,MaterialPass::Shadow,MaterialGeometry::Skinned,templates);
    Check(shadow.error.empty() && shadow.fragment.find("EvaluateSurface") == std::string::npos &&
          shadow.fragment.find("texture(") == std::string::npos && shadow.vertex.find("#define TE_GEOMETRY 1") != std::string::npos,
          "generated shadow shader keeps only opacity and selects skinned geometry");
    templates.files["Transform.glsl"] = "#include \"Transform.glsl\"\n";
    Check(!MaterialCompiler{}.GeneratePass(*result.material,MaterialPass::Shadow,MaterialGeometry::Static,templates).error.empty(),
          "cyclic shader template includes rejected");
}
void TestDynamicIsolation()
{
    ResourceManager resources; resources.Initialize(nullptr,root);
    MaterialSystem system(resources);
    Write("dynamic.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\n");
    auto red=system.CreateDynamicInstance("assets/dynamic.mat"), blue=system.CreateDynamicInstance("assets/dynamic.mat");
    Check(red && blue,"create dynamic instances from one asset");
    if (!red || !blue) return;
    Check(red->SetParameter("BaseColor",Vec4{1,0,0,1}) && blue->SetParameter("BaseColor",Vec4{0,0,1,1}),"set independent typed colors");
    const auto revision=red->GetRevision();
    Check(!red->SetParameter("BaseColor",.5f) && !red->SetParameter("Missing",1.f) && red->GetRevision()==revision,"bad setters keep revision unchanged");
    auto a=system.BuildProxy("assets/dynamic.mat",red.get()), b=system.BuildProxy("assets/dynamic.mat",blue.get());
    Check(a && b && a->compiled==b->compiled && a->canonicalValueContent!=b->canonicalValueContent,"same program, separate complete material identities");
    Check(system.BuildProxy("assets/dynamic.mat",red.get())==a,"unchanged dynamic instance reuses immutable snapshot");
    red->SetParameter("BaseColor",Vec4{0,0,1,1});
    Check(system.BuildProxy("assets/dynamic.mat",red.get())->canonicalValueContent==b->canonicalValueContent,"equal values can batch across instances");
    red->ClearOverride("BaseColor");
    Check(system.BuildProxy("assets/dynamic.mat",red.get())->canonicalValueContent==system.BuildProxy("assets/dynamic.mat")->canonicalValueContent,"reset restores inherited values");
    resources.Shutdown();
}
void TestReloadPublication()
{
    MaterialDefinitionData definition; definition.graph.SetFormatVersion(2); definition.sourcePath=root+"reload.matgraph";
    definition.graph.AddNode(MaterialNodeType::SurfaceOutput,"Surface");
    MaterialParameterDecl tint; tint.name="Tint"; tint.type=MaterialParameterType::Vec3; tint.defaultValue=Vec3{1,0,0}; definition.parameters={tint};
    const auto node=definition.graph.AddNode(MaterialNodeType::VectorParameter,"Tint"); definition.graph.GetNodes().back().parameterName="Tint"; definition.graph.Connect(node,1,0);
    definition.graph.GetParameters()=definition.parameters; Write("reload.matgraph",definition.graph.Serialize());
    Write("reloadA.mat","TINY_MAT 2\ndefinition \"reload.matgraph\"\n");
    Write("reloadB.mat","TINY_MAT 2\nparent \"reloadA.mat\"\nparameter \"Tint\" Vec3 0 0 1\n");
    ResourceManager resources; resources.Initialize(nullptr,root); MaterialSystem system(resources);
    const auto a=system.BuildProxy("assets/reloadA.mat"), b=system.BuildProxy("assets/reloadB.mat");
    const auto disk=PlatformFile::ReadAllText(root+"reload.matgraph"); definition.parameters[0].defaultValue=Vec3{0,1,0};
    const auto reject=[](const auto &,auto &diagnostics) { diagnostics.push_back({MaterialDiagnosticSeverity::Error,{},0,-1,"injected GPU candidate failure"}); return false; };
    Check(!system.ApplyDefinitionDraft("assets/reload.matgraph",definition,reject).success && system.BuildProxy("assets/reloadA.mat")==a && PlatformFile::ReadAllText(root+"reload.matgraph")==disk,"failed candidate keeps code/layout/values/textures/generation and disk together");
    const auto accept=[](const auto &,auto &) { return true; };
    Check(system.ApplyDefinitionDraft("assets/reload.matgraph",definition,accept).success,"valid parent draft publishes");
    const auto changed=system.BuildProxy("assets/reloadA.mat"), kept=system.BuildProxy("assets/reloadB.mat");
    std::cout << "Reload evidence: " << a->generation << " -> " << changed->generation << " values " << changed->numericSlots[0].x << ',' << changed->numericSlots[0].y << ',' << kept->numericSlots[0].z << " same program " << (changed->compiled==a->compiled) << '\n';
    Check(changed->compiled==a->compiled && changed->numericSlots[0].y==1 && kept->numericSlots[0].z==1 && changed->generation!=a->generation,"default changes reuse code, update unoverridden descendants and preserve child overrides");
    definition.parameters[0].type=MaterialParameterType::Float; definition.parameters[0].defaultValue=.25f; definition.graph.GetNodes()[1].type=MaterialNodeType::ScalarParameter;
    Check(system.ApplyDefinitionDraft("assets/reload.matgraph",definition,accept).success,"schema change can publish with old effective overrides inactive");
    Check(Near(system.BuildProxy("assets/reloadB.mat")->numericSlots[0].x,.25f) && !system.GetDiagnostics("assets/reloadB.mat").empty(),"mismatched old override becomes inactive and new default applies");
    Write("newInvalid.mat","TINY_MAT 2\ndefinition \"reload.matgraph\"\nparameter \"Unknown\" Float 1\n");
    Check(!system.Resolve("assets/newInvalid.mat"),"unknown override on first load remains an error");
    auto dynamic=system.CreateDynamicInstance("assets/reloadA.mat"); dynamic->SetParameter("Tint",.8f);
    definition.parameters.clear(); definition.graph.GetNodes().resize(1); definition.graph.GetLinks().clear();
    Check(system.ApplyDefinitionDraft("assets/reload.matgraph",definition,accept).success && !dynamic->SetParameter("Tint",.7f),"parameter deletion updates live dynamic schema");
    Check(system.BuildProxy("assets/reloadB.mat")->numericSlots.empty() && !system.GetDiagnostics("assets/reloadB.mat").empty() && system.BuildProxy("assets/reloadA.mat",dynamic.get())->numericSlots.empty(),"deleted declarations deactivate asset and dynamic overrides coherently");
    const auto active=system.BuildProxy("assets/reloadA.mat"); Write("reload.matgraph","TINY_MATGRAPH 999\n");
    Check(!system.Reload("assets/reload.matgraph",accept).success && system.BuildProxy("assets/reloadA.mat")==active,"broken external edit retains prior snapshot");
    Write("transaction.mat","color .9 .2 .1 1\n"); auto resource=resources.Load<MaterialResource>("assets/transaction.mat"); Write("transaction.mat","TINY_MAT 999\n");
    Check(resource && !resources.Reload("assets/transaction.mat") && resource->IsReady() && Near(resource->GetBaseColor().x,.9f),"generic material reload avoids clearing a valid resource before validation");
    Write("legacyDraft.mat","color .9 .2 .1 1\n"); const auto legacy=system.BuildProxy("assets/legacyDraft.mat");
    MaterialAssetData oldDraft; oldDraft.legacyData.source="color .1 .8 .2 1\n";
    Check(!system.ApplyInstanceDraft("assets/legacyDraft.mat",oldDraft,reject).success && system.BuildProxy("assets/legacyDraft.mat")==legacy,"legacy editor draft also rolls back before disk write");
    Check(system.ApplyInstanceDraft("assets/legacyDraft.mat",oldDraft,accept).success && PlatformFile::ReadAllText(root+"legacyDraft.mat")==oldDraft.legacyData.source,"legacy editor publishes from memory through the same transaction");
    resources.Shutdown();
}
}
int main()
{
    std::cout.setf(std::ios::unitbuf);
    try
    {
    std::filesystem::create_directories(root);
    TestLegacyMaterialCompatibility();
    TestInvalidMaterialCandidates();
    TestTypedMaterialRoundTrip();
    TestThreeLevelInheritance();
    TestParentCyclesAndDepth();
    TestAssetRootPaths();
    TestMaterialDependencies();
    TestGraphTypes();
    TestLegacyGraphUpgrade();
    TestReachableCycles();
    TestDeterministicCompilation();
    TestLimits();
    TestOpacityDependency();
    TestDynamicIsolation();
    TestReloadPublication();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] unhandled test error: " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
