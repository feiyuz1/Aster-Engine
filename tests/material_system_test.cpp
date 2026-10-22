#include "tinyengine/build/asset_compiler.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/material_system.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/resource/asset_maintenance.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/resource/resource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace tinyengine;
namespace
{
int failures = 0;
void check(bool condition, const char* label)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
    if (!condition)
        ++failures;
}
bool isClose(float a, float b)
{
    return std::abs(a - b) < 0.00001f;
}
const std::string root = "saved/MaterialTests/";
void write(const std::string& name, const std::string& text)
{
    check(PlatformFile::writeAllText(fromUtf8Path(root + name), text), "write material fixture");
}
void testLegacyMaterialCompatibility()
{
    write("parent.mat", "color .9 .2 .1 .8\nmetallic .8\nroughness .4\ntexture white.ppm\npackedTexture "
                        "channels.ppm\npackedLayout RMA\nblend alpha\n");
    write("child.mat", "parent parent.mat\noverride color 0\ncolor .1 .1 .1 1\nroughness .6\n");
    MaterialResource child;
    check(child.load(root + "child.mat"), "legacy parent loads");
    check(isClose(child.getBaseColor().x, .9f) && isClose(child.getMetallic(), .8f) &&
              isClose(child.getRoughness(), .6f),
          "legacy false override inherits; missing override applies");
    check(child.GetTexturePath() == "white.ppm" && child.GetPackedTexturePath() == "channels.ppm" &&
              child.GetPackedTextureLayout() == "RMA" && child.isTransparent(),
          "legacy texture, packed layout and blend survive");
    write("implicitAlpha.mat", "color 1 1 1 .5\n");
    MaterialResource alpha;
    alpha.load(root + "implicitAlpha.mat");
    check(alpha.GetMaterialData().legacyData.transparent, "legacy color alpha preserves implicit transparent state");
}
void testInvalidMaterialCandidates()
{
    write("valid.mat", "color .91 .32 .14 1\nmetallic .75\n");
    MaterialResource material;
    check(material.load(root + "valid.mat"), "load valid material before failed replacement");
    check(!material.load(root + "missing.mat"), "missing material fails");
    check(material.isReady() && isClose(material.getBaseColor().x, .91f) && isClose(material.getMetallic(), .75f),
          "failed replacement retains previous ready material");
    write("version.mat", "TINY_MAT 99\ndefinition \"builtin:PBR\"\n");
    check(!material.load(root + "version.mat"), "unknown material version is rejected");
    write("duplicate.mat",
          "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"Metallic\" Float .1\nparameter \"Metallic\" Float .2\n");
    check(!material.load(root + "duplicate.mat"), "duplicate typed overrides are rejected");
    write("ambiguous.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparent \"parent.mat\"\n");
    check(!material.load(root + "ambiguous.mat"), "definition and parent cannot coexist");
}
void testTypedMaterialRoundTrip()
{
    write("typed.mat",
          "TINY_MAT 2\ndefinition \"assets/材质 库/Parent.matgraph\"\n"
          "parameter \"Weight\" Float .25\nparameter \"Scale\" Vec2 2 3\nparameter \"Emission\" Vec3 4 5 6\n"
          "parameter \"Tint\" Vec4 .1 .2 .3 .4\ntexture \"BaseMap\" \"assets/贴图 库/图片.png\"\n");
    MaterialResource material;
    check(material.load(root + "typed.mat"), "typed material loads without requiring definition yet");
    const auto& data = material.GetMaterialData();
    check(data.formatVersion == 2 && data.definitionPath == "assets/材质 库/Parent.matgraph" &&
              data.overrides.size() == 5,
          "typed asset keeps quoted paths and overrides");
    check(std::holds_alternative<float>(data.overrides.at("Weight")) &&
              std::holds_alternative<Vec2>(data.overrides.at("Scale")) &&
              isClose(std::get<Vec2>(data.overrides.at("Scale")).y, 3) &&
              isClose(std::get<Vec3>(data.overrides.at("Emission")).z, 6) &&
              isClose(std::get<Vec4>(data.overrides.at("Tint")).w, .4f) &&
              std::get<std::string>(data.overrides.at("BaseMap")) == "assets/贴图 库/图片.png",
          "five distinct parameter types parsed");
    check(material.Save(root + "roundtrip.mat"), "save typed instance");
    MaterialResource reopened;
    check(reopened.load(root + "roundtrip.mat") && reopened.GetMaterialData().definitionPath == data.definitionPath &&
              isClose(std::get<Vec4>(reopened.GetMaterialData().overrides.at("Tint")).z, .3f) &&
              std::get<std::string>(reopened.GetMaterialData().overrides.at("BaseMap")) ==
                  std::get<std::string>(data.overrides.at("BaseMap")),
          "typed values and unicode paths survive save and reopen");
    write("malformed.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"te_internal\" Float .4\n");
    check(!reopened.load(root + "malformed.mat"), "reserved parameter prefix rejected");
    write("truncated.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"Tint\" Vec4 1 2\n");
    check(!reopened.load(root + "truncated.mat"), "truncated typed value rejected");
}
const MaterialParameterValue& Value(const ResolvedMaterial& material, const std::string& name)
{
    const auto& declarations = material.definition->parameters;
    for (std::size_t i = 0; i < declarations.size(); ++i)
        if (declarations[i].name == name)
            return material.values.at(i);
    throw std::runtime_error("Missing resolved parameter " + name);
}
void writeDefinition()
{
    write("assets/材质 库/paint.matgraph",
          "TINY_MATGRAPH 2\nsettings Lit Opaque 0 .5\n"
          "parameter \"Tint\" Vec3 Linear .8 .7 .6\nparameter_meta \"Tint\" \"颜色\" \"外观\" 0 0 1\n"
          "parameter \"Roughness\" Float Linear .7\nparameter \"BaseMap\" Texture2D SRgb \"../贴图 库/base.ppm\"\n"
          "node 1 SurfaceOutput \"输出\" 100 0 Float 0 0 0 0 \"\" \"\"\n");
    write("assets/贴图 库/base.ppm", "P3\n1 1\n255\n255 255 255\n");
}
void testThreeLevelInheritance()
{
    writeDefinition();
    write("assets/parent.mat",
          "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Tint\" Vec3 1 .1 .2\n");
    write("assets/middle.mat", "TINY_MAT 2\nparent \"./parent.mat\"\nparameter \"Roughness\" Float .3\n");
    write("assets/child.mat", "TINY_MAT 2\nparent \"middle.mat\"\nparameter \"Tint\" Vec3 .1 .2 1\n");
    ResourceManager resources;
    resources.initialize(nullptr, root + "assets");
    MaterialSystem system(resources);
    auto child = system.resolve("assets/child.mat");
    check(child && isClose(std::get<Vec3>(Value(*child, "Tint")).z, 1) &&
              isClose(std::get<float>(Value(*child, "Roughness")), .3f),
          "three level child to parent to definition precedence");
    write("assets/reset.mat", "TINY_MAT 2\nparent \"middle.mat\"\n");
    auto reset = system.resolve("assets/reset.mat");
    check(reset && isClose(std::get<Vec3>(Value(*reset, "Tint")).x, 1),
          "absent child override restores inherited value");
    write("assets/unknown.mat",
          "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Unknown\" Float 1\n");
    check(!system.resolve("assets/unknown.mat"), "first load rejects unknown parameter");
    write("assets/wrong.mat",
          "TINY_MAT 2\ndefinition \"assets/材质 库/paint.matgraph\"\nparameter \"Tint\" Vec4 1 1 1 1\n");
    check(!system.resolve("assets/wrong.mat"), "first load rejects wrong override type");
    auto legacy = system.resolve(std::filesystem::absolute(root + "child.mat").generic_string());
    check(legacy && isClose(std::get<Vec4>(Value(*legacy, "BaseColor")).x, .9f),
          "legacy adapter keeps false override inheritance");
}
void testParentCyclesAndDepth()
{
    ResourceManager resources;
    resources.initialize(nullptr, root + "assets");
    MaterialSystem system(resources);
    write("assets/a.mat", "TINY_MAT 2\nparent \"./b.mat\"\n");
    write("assets/b.mat", "TINY_MAT 2\nparent \"../assets/a.mat\"\n");
    check(!system.resolve("assets/a.mat"), "normalized parent cycle rejected");
    const auto& diagnostics = system.GetDiagnostics("assets/a.mat");
    check(!diagnostics.empty() && diagnostics.front().message.find("a.mat") != std::string::npos &&
              diagnostics.front().message.find("b.mat") != std::string::npos,
          "cycle diagnostic contains complete chain");
    for (int i = 0; i < 33; ++i)
        write("assets/depth" + std::to_string(i) + ".mat",
              "TINY_MAT 2\n" + (i == 0 ? std::string("definition \"builtin:PBR\"\n")
                                       : "parent \"depth" + std::to_string(i - 1) + ".mat\"\n"));
    check(static_cast<bool>(system.resolve("assets/depth31.mat")), "32 instance nodes accepted");
    check(!system.resolve("assets/depth32.mat"), "33 instance nodes rejected even with cached parent");
    write("assets/legacy0.mat", "color .9 .2 .1 1\n");
    for (int i = 1; i < 32; ++i)
        write("assets/legacy" + std::to_string(i) + ".mat", "parent legacy" + std::to_string(i - 1) + ".mat\n");
    write("assets/mixedDepth.mat", "TINY_MAT 2\nparent \"legacy31.mat\"\n");
    check(!system.resolve("assets/mixedDepth.mat"), "depth budget includes legacy nodes beneath a typed instance");
}
void testAssetRootPaths()
{
    const auto previous = std::filesystem::current_path();
    ResourceManager resources;
    resources.initialize(nullptr, root + "assets");
    std::filesystem::current_path(previous / "build");
    MaterialSystem system(resources);
    auto material = system.resolve("assets/child.mat");
    check(material && PlatformFile::exists(fromUtf8Path(std::get<std::string>(Value(*material, "BaseMap")))),
          "asset root and relative unicode texture survive CWD change");
    std::filesystem::current_path(previous);
}
void testMaterialDependencies()
{
    const auto assetRoot = std::filesystem::absolute(root + "assets");
    AssetRegistry registry;
    registry.scan(assetRoot, false);
    const auto parentPath = (assetRoot / "parent.mat").generic_string();
    const auto graphPath = toUtf8GenericPath((assetRoot / fromUtf8Path("材质 库/paint.matgraph")));
    const auto childPath = (assetRoot / "child.mat").generic_string();
    auto parent = registry.Find(parentPath);
    auto graph = registry.Find(graphPath);
    check(parent && std::find(parent->dependencies.begin(), parent->dependencies.end(), graphPath) !=
                        parent->dependencies.end(),
          "material indexes definition dependency");
    check(graph && graph->dependencies.size() == 1 && PlatformFile::exists(fromUtf8Path(graph->dependencies[0])),
          "graph indexes default texture");
    const auto dependents = registry.GetDependents((assetRoot / "middle.mat").generic_string());
    check(std::find(dependents.begin(), dependents.end(), childPath) != dependents.end(),
          "reverse dependency index finds child instance");
    const auto moved = assetRoot / fromUtf8Path("材质 库/moved.matgraph");
    if (std::filesystem::exists(moved))
        std::filesystem::remove(moved);
    auto movedMeta = moved;
    movedMeta += ".meta";
    if (std::filesystem::exists(movedMeta))
        std::filesystem::remove(movedMeta);
    std::string error;
    check(AssetMaintenance::moveAsset(registry, fromUtf8Path(graphPath), moved, &error), "move definition asset");
    MaterialResource reopened;
    check(reopened.load(parentPath) &&
              reopened.GetMaterialData().definitionPath.find("moved.matgraph") != std::string::npos,
          "asset move rewrites structured logical definition reference");
}
MaterialDefinitionData graphDefinition()
{
    MaterialDefinitionData data;
    data.sourcePath = "test:graph";
    data.graph.setFormatVersion(2);
    data.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    return data;
}
std::uint32_t Constant(MaterialDefinitionData& data, MaterialParameterType type, Vec4 value)
{
    const auto id = data.graph.addNode(MaterialNodeType::Constant, "Constant");
    auto& node = data.graph.GetNodes().back();
    node.constantType = type;
    node.value = value;
    return id;
}
MaterialParameterDecl declaration(const std::string& name, MaterialParameterValue value)
{
    MaterialParameterDecl p;
    p.name = name;
    p.type = getMaterialParameterType(value);
    p.defaultValue = std::move(value);
    return p;
}
std::uint32_t parameter(MaterialDefinitionData& data, const std::string& name, MaterialParameterValue value)
{
    const auto type = getMaterialParameterType(value);
    data.parameters.push_back(declaration(name, std::move(value)));
    const auto nodeType = type == MaterialParameterType::Texture2D ? MaterialNodeType::TextureParameter
                          : type == MaterialParameterType::Float   ? MaterialNodeType::ScalarParameter
                                                                   : MaterialNodeType::VectorParameter;
    const auto id = data.graph.addNode(nodeType, name);
    data.graph.GetNodes().back().parameterName = name;
    return id;
}
std::uint32_t mask(MaterialDefinitionData& data, std::uint32_t from, const std::string& channels)
{
    const auto id = data.graph.addNode(MaterialNodeType::ComponentMask, "Mask");
    data.graph.GetNodes().back().channelMask = channels;
    data.graph.connect(from, id, 0);
    return id;
}
void testGraphTypes()
{
    MaterialCompiler compiler;
    auto graph = graphDefinition();
    const auto vector = Constant(graph, MaterialParameterType::Vec4, {1, .3f, .2f, .7f});
    graph.graph.connect(vector, 1, 0);
    check(!compiler.compile(graph).Success(), "Vec4 to BaseColor requires explicit component mask");
    graph.graph.connect(mask(graph, vector, "xyz"), 1, 0);
    check(compiler.compile(graph).Success(), "component mask provides typed BaseColor");
    auto numeric = graphDefinition();
    const auto v = Constant(numeric, MaterialParameterType::Vec3, {.2f, .4f, .6f, 0});
    const auto scalar = parameter(numeric, "Scale", .5f);
    const auto multiply = numeric.graph.addNode(MaterialNodeType::Multiply, "Multiply");
    numeric.graph.connect(v, multiply, 0);
    numeric.graph.connect(scalar, multiply, 1);
    const auto add = numeric.graph.addNode(MaterialNodeType::Add, "Add");
    numeric.graph.connect(multiply, add, 0);
    numeric.graph.connect(scalar, add, 1);
    numeric.graph.connect(add, 1, 0);
    check(compiler.compile(numeric).Success(), "Add and Multiply broadcast Float to Vec3");
    const auto oneMinus = numeric.graph.addNode(MaterialNodeType::OneMinus, "OneMinus");
    numeric.graph.connect(add, oneMinus, 0);
    const auto clamp = numeric.graph.addNode(MaterialNodeType::Clamp, "Clamp");
    numeric.graph.connect(oneMinus, clamp, 0);
    const auto lerp = numeric.graph.addNode(MaterialNodeType::Lerp, "Lerp");
    numeric.graph.connect(clamp, lerp, 0);
    numeric.graph.connect(scalar, lerp, 1);
    numeric.graph.connect(lerp, 1, 0);
    check(compiler.compile(numeric).Success(), "OneMinus, Clamp and Lerp support typed values and defaults");
    const auto bad = Constant(numeric, MaterialParameterType::Vec2, {1, 2, 0, 0});
    numeric.graph.connect(bad, lerp, 2);
    check(!compiler.compile(numeric).Success(), "Lerp alpha must be Float");
    numeric.graph.connect(scalar, lerp, 2);
    numeric.graph.connect(bad, clamp, 1);
    check(!compiler.compile(numeric).Success(), "Clamp minimum must be Float");
    auto sampled = graphDefinition();
    const auto texture = parameter(sampled, "Normal", std::string("normal.ppm"));
    sampled.parameters.back().sampling = TextureSampling::Normal;
    const auto uv = sampled.graph.addNode(MaterialNodeType::TexCoord, "UV0");
    const auto sample = sampled.graph.addNode(MaterialNodeType::TextureSample, "Sample");
    sampled.graph.connect(texture, sample, 0);
    sampled.graph.connect(uv, sample, 1);
    const auto normal = sampled.graph.addNode(MaterialNodeType::NormalDecode, "NormalDecode");
    sampled.graph.connect(mask(sampled, sample, "xyz"), normal, 0);
    sampled.graph.connect(normal, 1, 3);
    check(compiler.compile(sampled).Success(), "TextureParameter, TextureSample, UV0 and NormalDecode compile");
    auto badUv = sampled;
    const auto floatUv = Constant(badUv, MaterialParameterType::Float, {1, 0, 0, 0});
    badUv.graph.connect(floatUv, sample, 1);
    check(!compiler.compile(badUv).Success(), "texture UV requires Vec2");
    auto absent = graphDefinition();
    const auto missing = absent.graph.addNode(MaterialNodeType::TextureSample, "Missing");
    absent.graph.connect(mask(absent, missing, "xyz"), 1, 0);
    check(!compiler.compile(absent).Success(), "texture sample requires texture input");
    auto vectorParam = graphDefinition();
    vectorParam.graph.connect(parameter(vectorParam, "Color", Vec3{1, .2f, .3f}), 1, 0);
    check(compiler.compile(vectorParam).Success(), "VectorParameter resolves declared vector type");
    auto invalidMask = graph;
    invalidMask.graph.GetNodes().back().channelMask = "xa";
    check(!compiler.compile(invalidMask).Success(), "invalid component mask is rejected");
    auto invalidUV = sampled;
    for (auto& node : invalidUV.graph.GetNodes())
        if (node.id == uv)
            node.uvIndex = 1;
    check(!compiler.compile(invalidUV).Success(), "only UV0 is supported");
    auto duplicate = graph;
    duplicate.graph.GetNodes().push_back(duplicate.graph.GetNodes().back());
    check(!compiler.compile(duplicate).Success(), "duplicate node IDs rejected");
    auto port = graph;
    port.graph.GetLinks().front().fromOutput = 1;
    check(!compiler.compile(port).Success(), "unknown output port rejected");
    auto input = graph;
    input.graph.GetLinks().push_back(input.graph.GetLinks().front());
    check(!compiler.compile(input).Success(), "duplicate input links rejected");
}
void testLegacyGraphUpgrade()
{
    write("legacy.matgraph",
          "TINY_MATGRAPH 1\nnode 1 0 \"Color\" 0 0 .2 .4 .6 .8 \"\"\n"
          "node 2 4 \"Output\" 100 0 1 1 1 1 \"\"\nlink 1 2 0\nlink 1 2 1\nlink 1 2 2\nlink 1 2 3\n");
    MaterialDefinitionResource resource;
    check(resource.load(root + "legacy.matgraph"), "load v1 graph definition");
    const auto result = MaterialCompiler{}.compile(resource.GetData());
    check(result.Success() && result.material->surfaceSource.find(".xyz") != std::string::npos &&
              result.material->opacitySource.find(".w") != std::string::npos,
          "legacy RGB and alpha are explicitly mapped");
    auto upgraded = resource.GetData().graph;
    check(upgraded.upgradeLegacy() && upgraded.getFormatVersion() == 2 && upgraded.Save(root + "upgraded.matgraph"),
          "explicit legacy graph save uses v2");
    MaterialDefinitionResource reopened;
    check(reopened.load(root + "upgraded.matgraph") && MaterialCompiler{}.compile(reopened.GetData()).Success(),
          "upgraded graph reopens and compiles");
    MaterialDefinitionData emptyLegacy;
    const auto add = emptyLegacy.graph.addNode(MaterialNodeType::Add, "Unconnected Add");
    const auto output = emptyLegacy.graph.addNode(MaterialNodeType::PbrOutput, "Output");
    emptyLegacy.graph.connect(add, output, 0);
    check(MaterialCompiler{}.compile(emptyLegacy).Success(), "legacy unconnected arithmetic retains Vec4 defaults");
    write("legacyTexture.matgraph", "TINY_MATGRAPH 1\nnode 1 1 \"Map\" 0 0 1 1 1 1 \"assets/贴图 库/base.ppm\"\n"
                                    "node 2 4 \"Output\" 100 0 1 1 1 1 \"\"\nlink 1 2 0\n");
    MaterialDefinitionResource textureDefinition;
    check(textureDefinition.load(root + "legacyTexture.matgraph") &&
              textureDefinition.GetData().parameters.size() == 1 &&
              textureDefinition.GetData().parameters[0].type == MaterialParameterType::Texture2D,
          "legacy texture nodes expose declarations to the inheritance resolver");
}
void testReachableCycles()
{
    auto graph = graphDefinition();
    const auto a = graph.graph.addNode(MaterialNodeType::Add, "A"), b = graph.graph.addNode(MaterialNodeType::Add, "B");
    graph.graph.connect(a, b, 0);
    graph.graph.connect(b, a, 0);
    graph.graph.connect(a, 1, 2);
    check(!MaterialCompiler{}.compile(graph).Success(), "reachable cycle fails compilation");
    graph.graph.GetLinks().pop_back();
    const auto result = MaterialCompiler{}.compile(graph);
    check(result.Success() &&
              std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                          [](const auto& d) { return d.severity == MaterialDiagnosticSeverity::Warning; }),
          "unreachable cycle produces warning and no shader code");
}
void testDeterministicCompilation()
{
    auto graph = graphDefinition();
    const auto p = parameter(graph, "Rough", .25f);
    graph.graph.connect(p, 1, 2);
    const auto c = Constant(graph, MaterialParameterType::Vec3, {.1f, .2f, .3f, 0});
    graph.graph.connect(c, 1, 0);
    auto first = MaterialCompiler{}.compile(graph);
    auto edited = graph;
    edited.parameters[0].defaultValue = .8f;
    edited.parameters[0].displayName = "中文显示名";
    edited.graph.GetNodes()[1].position = {500, 300};
    edited.renderState.twoSided = true;
    edited.renderState.maskCutoff = .8f;
    auto second = MaterialCompiler{}.compile(edited);
    check(first.Success() && second.Success() &&
              first.material->canonicalProgramContent == second.material->canonicalProgramContent,
          "layout and program ignore parameter defaults, editor position and raster uniforms");
    edited.graph.GetNodes().back().value.x = .7f;
    auto changed = MaterialCompiler{}.compile(edited);
    check(changed.Success() && changed.material->canonicalProgramContent != first.material->canonicalProgramContent,
          "literal constant participates in program identity");
    auto sorted = graphDefinition();
    sorted.parameters = {declaration("Z", 1.f), declaration("A", Vec2{2, 3})};
    auto ordered = MaterialCompiler{}.compile(sorted);
    std::reverse(sorted.parameters.begin(), sorted.parameters.end());
    auto reversed = MaterialCompiler{}.compile(sorted);
    check(ordered.Success() && reversed.Success() && ordered.material->layout.numericSlots[0].name == "A" &&
              ordered.material->canonicalProgramContent == reversed.material->canonicalProgramContent,
          "declaration ordering produces stable layout");
}
void testLimits()
{
    auto graph = graphDefinition();
    for (int i = 0; i < 64; ++i)
        graph.parameters.push_back(declaration("P" + std::to_string(i), 0.f));
    for (int i = 0; i < 8; ++i)
        graph.parameters.push_back(declaration("T" + std::to_string(i), std::string{}));
    check(MaterialCompiler{}.compile(graph).Success(), "64 numeric plus 8 texture declarations accepted");
    auto excess = graph;
    excess.parameters.push_back(declaration("ExtraNumber", 0.f));
    check(!MaterialCompiler{}.compile(excess).Success(), "65th numeric declaration rejected");
    excess = graph;
    excess.parameters.push_back(declaration("ExtraTexture", std::string{}));
    check(!MaterialCompiler{}.compile(excess).Success(), "9th texture declaration rejected");
    excess = graph;
    excess.parameters.push_back(excess.parameters[0]);
    check(!MaterialCompiler{}.compile(excess).Success(), "duplicate parameter declaration rejected");
    excess = graphDefinition();
    excess.parameters.push_back(declaration("te_reserved", 0.f));
    check(!MaterialCompiler{}.compile(excess).Success(), "reserved parameter declaration rejected");
    excess = graphDefinition();
    excess.parameters.push_back(declaration("Number", 0.f));
    excess.parameters[0].sampling = TextureSampling::Normal;
    check(!MaterialCompiler{}.compile(excess).Success(), "numeric parameter sampling must be Linear");
    excess = graphDefinition();
    excess.renderState.maskCutoff = std::numeric_limits<float>::quiet_NaN();
    check(!MaterialCompiler{}.compile(excess).Success(), "non-finite render settings rejected before GPU upload");
}
void testOpacityDependency()
{
    auto graph = graphDefinition();
    graph.renderState.blendMode = MaterialBlendMode::Masked;
    const auto texture = parameter(graph, "EmissionMap", std::string("emissive.ppm"));
    const auto sample = graph.graph.addNode(MaterialNodeType::TextureSample, "Emission Sample");
    graph.graph.connect(texture, sample, 0);
    graph.graph.connect(mask(graph, sample, "xyz"), 1, 5);
    graph.graph.connect(parameter(graph, "Alpha", .8f), 1, 6);
    const auto result = MaterialCompiler{}.compile(graph);
    check(result.Success() && result.material->surfaceSource.find("texture(") != std::string::npos &&
              result.material->opacitySource.find("texture(") == std::string::npos,
          "opacity compilation prunes emission-only texture sampling");
    MaterialTemplateSources templates;
    templates.files = {
        {"BasePass.vert", "#version 430 core\n#include \"Transform.glsl\"\n"},
        {"Transform.glsl", "void main() { gl_Position=vec4(0.0); }\n"},
        {"Surface.glsl", "struct SurfaceData { vec3 BaseColor; };\n"},
        {"ShadowPass.vert", "#version 430 core\n#include \"Transform.glsl\"\n"},
        {"ShadowPass.frag", "#version 430 core\n/*MATERIAL_DECLARATIONS*/\n/*MATERIAL_FUNCTIONS*/\nvoid main() {}\n"}};
    const auto shadow =
        MaterialCompiler{}.generatePass(*result.material, MaterialPass::Shadow, MaterialGeometry::Skinned, templates);
    check(shadow.error.empty() && shadow.fragment.find("EvaluateSurface") == std::string::npos &&
              shadow.fragment.find("texture(") == std::string::npos &&
              shadow.vertex.find("#define TE_GEOMETRY 1") != std::string::npos,
          "generated shadow shader keeps only opacity and selects skinned geometry");
    templates.files["Transform.glsl"] = "#include \"Transform.glsl\"\n";
    check(!MaterialCompiler{}
               .generatePass(*result.material, MaterialPass::Shadow, MaterialGeometry::Static, templates)
               .error.empty(),
          "cyclic shader template includes rejected");
}
void testDynamicIsolation()
{
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    write("dynamic.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\n");
    auto red = system.CreateDynamicInstance("assets/dynamic.mat"),
         blue = system.CreateDynamicInstance("assets/dynamic.mat");
    check(red && blue, "create dynamic instances from one asset");
    if (!red || !blue)
        return;
    check(red->setParameter("BaseColor", Vec4{1, 0, 0, 1}) && blue->setParameter("BaseColor", Vec4{0, 0, 1, 1}),
          "set independent typed colors");
    const auto revision = red->getRevision();
    check(!red->setParameter("BaseColor", .5f) && !red->setParameter("Missing", 1.f) && red->getRevision() == revision,
          "bad setters keep revision unchanged");
    auto a = system.BuildProxy("assets/dynamic.mat", red.get()),
         b = system.BuildProxy("assets/dynamic.mat", blue.get());
    check(a && b && a->compiled == b->compiled && a->canonicalValueContent != b->canonicalValueContent,
          "same program, separate complete material identities");
    check(system.BuildProxy("assets/dynamic.mat", red.get()) == a,
          "unchanged dynamic instance reuses immutable snapshot");
    red->setParameter("BaseColor", Vec4{0, 0, 1, 1});
    check(system.BuildProxy("assets/dynamic.mat", red.get())->canonicalValueContent == b->canonicalValueContent,
          "equal values can batch across instances");
    red->clearOverride("BaseColor");
    check(system.BuildProxy("assets/dynamic.mat", red.get())->canonicalValueContent ==
              system.BuildProxy("assets/dynamic.mat")->canonicalValueContent,
          "reset restores inherited values");
    resources.shutdown();
}
void testReloadPublication()
{
    MaterialDefinitionData definition;
    definition.graph.setFormatVersion(2);
    definition.sourcePath = root + "reload.matgraph";
    definition.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    MaterialParameterDecl tint;
    tint.name = "Tint";
    tint.type = MaterialParameterType::Vec3;
    tint.defaultValue = Vec3{1, 0, 0};
    definition.parameters = {tint};
    const auto node = definition.graph.addNode(MaterialNodeType::VectorParameter, "Tint");
    definition.graph.GetNodes().back().parameterName = "Tint";
    definition.graph.connect(node, 1, 0);
    definition.graph.GetParameters() = definition.parameters;
    write("reload.matgraph", definition.graph.serialize());
    write("reloadA.mat", "TINY_MAT 2\ndefinition \"reload.matgraph\"\n");
    write("reloadB.mat", "TINY_MAT 2\nparent \"reloadA.mat\"\nparameter \"Tint\" Vec3 0 0 1\n");
    write("reloadUnloaded.mat", "TINY_MAT 2\ndefinition \"reload.matgraph\"\nparameter \"Tint\" Vec3 0 0 1\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    const auto a = system.BuildProxy("assets/reloadA.mat"), b = system.BuildProxy("assets/reloadB.mat");
    const auto disk = PlatformFile::readAllText(root + "reload.matgraph");
    definition.parameters[0].defaultValue = Vec3{0, 1, 0};
    const auto reject = [](const auto&, auto& diagnostics)
    {
        diagnostics.push_back({MaterialDiagnosticSeverity::Error, {}, 0, -1, "injected GPU candidate failure"});
        return false;
    };
    check(!system.applyDefinitionDraft("assets/reload.matgraph", definition, reject).success &&
              system.BuildProxy("assets/reloadA.mat") == a &&
              PlatformFile::readAllText(root + "reload.matgraph") == disk,
          "failed candidate keeps code/layout/values/textures/generation and disk together");
    const auto accept = [](const auto&, auto&) { return true; };
#ifdef _WIN32
    const auto childFile = std::filesystem::absolute(root + "reloadB.mat");
    const auto childBefore = PlatformFile::readAllText(childFile);
    const auto reader = CreateFileW(childFile.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(reader != INVALID_HANDLE_VALUE, "lock a dependent while publishing schema provenance");
    const auto blocked = system.applyDefinitionDraft("assets/reload.matgraph", definition, accept);
    if (reader != INVALID_HANDLE_VALUE)
        CloseHandle(reader);
    check(!blocked.success && PlatformFile::readAllText(root + "reload.matgraph") == disk &&
              PlatformFile::readAllText(childFile) == childBefore && system.BuildProxy("assets/reloadA.mat") == a,
          "failed dependent provenance write restores parent/children and live generation together");
#endif
    check(system.applyDefinitionDraft("assets/reload.matgraph", definition, accept).success,
          "valid parent draft publishes");
    const auto changed = system.BuildProxy("assets/reloadA.mat"), kept = system.BuildProxy("assets/reloadB.mat");
    std::cout << "Reload evidence: " << a->generation << " -> " << changed->generation << " values "
              << changed->numericSlots[0].x << ',' << changed->numericSlots[0].y << ',' << kept->numericSlots[0].z
              << " same program " << (changed->compiled == a->compiled) << '\n';
    check(changed->compiled == a->compiled && changed->numericSlots[0].y == 1 && kept->numericSlots[0].z == 1 &&
              changed->generation != a->generation,
          "default changes reuse code, update unoverridden descendants and preserve child overrides");
    definition.parameters[0].type = MaterialParameterType::Float;
    definition.parameters[0].defaultValue = .25f;
    definition.graph.GetNodes()[1].type = MaterialNodeType::ScalarParameter;
    check(system.applyDefinitionDraft("assets/reload.matgraph", definition, accept).success,
          "schema change can publish with old effective overrides inactive");
    check(isClose(system.BuildProxy("assets/reloadB.mat")->numericSlots[0].x, .25f) &&
              !system.GetDiagnostics("assets/reloadB.mat").empty(),
          "mismatched old override becomes inactive and new default applies");
    {
        ResourceManager freshResources;
        freshResources.initialize(nullptr, root);
        MaterialSystem fresh(freshResources);
        const auto restarted = fresh.resolve("assets/reloadB.mat");
        check(restarted && !fresh.GetDiagnostics("assets/reloadB.mat").empty(),
              "schema invalidation remains recoverable after a fresh material system");
        check(fresh.resolve("assets/reloadUnloaded.mat") != nullptr,
              "schema migration covers registered descendants that were not loaded in the scene");
        check(PlatformAssetCompiler::compile(root + "reloadB.mat", root + "Cooked/reloadB.mat", root).success,
              "offline packaging accepts persisted inactive overrides after schema change");
        freshResources.shutdown();
    }
    write("newInvalid.mat", "TINY_MAT 2\ndefinition \"reload.matgraph\"\nparameter \"Unknown\" Float 1\n");
    check(!system.resolve("assets/newInvalid.mat"), "unknown override on first load remains an error");
    auto dynamic = system.CreateDynamicInstance("assets/reloadA.mat");
    dynamic->setParameter("Tint", .8f);
    definition.parameters.clear();
    definition.graph.GetNodes().resize(1);
    definition.graph.GetLinks().clear();
    check(system.applyDefinitionDraft("assets/reload.matgraph", definition, accept).success &&
              !dynamic->setParameter("Tint", .7f),
          "parameter deletion updates live dynamic schema");
    check(system.BuildProxy("assets/reloadB.mat")->numericSlots.empty() &&
              !system.GetDiagnostics("assets/reloadB.mat").empty() &&
              system.BuildProxy("assets/reloadA.mat", dynamic.get())->numericSlots.empty(),
          "deleted declarations deactivate asset and dynamic overrides coherently");
    const auto active = system.BuildProxy("assets/reloadA.mat");
    write("reload.matgraph", "TINY_MATGRAPH 999\n");
    check(!system.reload("assets/reload.matgraph", accept).success && system.BuildProxy("assets/reloadA.mat") == active,
          "broken external edit retains prior snapshot");
    write("transaction.mat", "color .9 .2 .1 1\n");
    auto resource = resources.load<MaterialResource>("assets/transaction.mat");
    write("transaction.mat", "TINY_MAT 999\n");
    check(resource && !resources.reload("assets/transaction.mat") && resource->isReady() &&
              isClose(resource->getBaseColor().x, .9f),
          "generic material reload avoids clearing a valid resource before validation");
    write("legacyDraft.mat", "color .9 .2 .1 1\n");
    const auto legacy = system.BuildProxy("assets/legacyDraft.mat");
    MaterialAssetData oldDraft;
    oldDraft.legacyData.source = "color .1 .8 .2 1\n";
    check(!system.applyInstanceDraft("assets/legacyDraft.mat", oldDraft, reject).success &&
              system.BuildProxy("assets/legacyDraft.mat") == legacy,
          "legacy editor draft also rolls back before disk write");
    check(system.applyInstanceDraft("assets/legacyDraft.mat", oldDraft, accept).success &&
              PlatformFile::readAllText(root + "legacyDraft.mat") == oldDraft.legacyData.source,
          "legacy editor publishes from memory through the same transaction");
    resources.shutdown();
}
void testCookingAndSourceCache()
{
    write("invalidCook.matgraph", "TINY_MATGRAPH 99\n");
    check(!PlatformAssetCompiler::compile(root + "invalidCook.matgraph", root + "Cooked/invalid.matgraph").success,
          "offline cook rejects invalid graph rather than copying it");
    auto graph = makeBuiltinPbrDefinition();
    const auto compiled = MaterialCompiler{}.compile(graph);
    auto templates = ShaderPipeline::loadMaterialTemplates("missing-material-template-directory");
    std::string first, second, third;
    check(MaterialCompiler{}.writeCache(*compiled.material, templates, root + "SourceCache", &first),
          "write generated GLSL source/layout/dependency cache");
    check(MaterialCompiler{}.writeCache(*compiled.material, templates, root + "SourceCache", &second) &&
              first == second,
          "same template content reuses cache identity");
#ifdef _WIN32
    const auto cached = std::filesystem::absolute(root + "SourceCache/" + first + "/content.key");
    const auto timestamp = std::filesystem::last_write_time(cached);
    const auto reader = CreateFileW(cached.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(reader != INVALID_HANDLE_VALUE, "open an existing cache entry for reading without delete sharing");
    const bool reused = MaterialCompiler{}.writeCache(*compiled.material, templates, root + "SourceCache", &second);
    if (reader != INVALID_HANDLE_VALUE)
        CloseHandle(reader);
    check(reused && second == first && std::filesystem::last_write_time(cached) == timestamp,
          "reuse complete cache while readers hold existing source; leave timestamps unchanged");
#endif
    templates.files["Lighting.glsl"] += "\n// changed include content\n";
    check(MaterialCompiler{}.writeCache(*compiled.material, templates, root + "SourceCache", &third) && third != first,
          "included template content invalidates generated source cache");
    check(std::filesystem::is_regular_file(fromUtf8Path(root + "SourceCache/" + third + "/Base-Static.frag")),
          "cache stores inspectable source rather than driver binaries");
}
void testMoveReferenceRollback()
{
#ifdef _WIN32
    const auto directory = std::filesystem::absolute(root + "MoveRollback" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(directory);
    const auto source = directory / "old.ppm", destination = directory / "new.ppm", owner = directory / "owner.mat";
    PlatformFile::writeAllText(source, "P3\n1 1\n255\n255 255 255\n");
    const std::string content = "TINY_MAT 2\ndefinition \"builtin:PBR\"\ntexture \"BaseMap\" \"old.ppm\"\n";
    PlatformFile::writeAllText(owner, content);
    AssetRegistry registry;
    registry.scan(directory);
    const auto handle = CreateFileW(owner.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "lock an owner file against replacement");
    if (handle == INVALID_HANDLE_VALUE)
        return;
    std::string error;
    const bool moved = AssetMaintenance::moveAsset(registry, source, destination, &error);
    CloseHandle(handle);
    check(!moved && !error.empty() && std::filesystem::exists(source) && !std::filesystem::exists(destination) &&
              PlatformFile::readAllText(owner) == content,
          "failed owner rewrite restores asset/meta and preserves original references");
#endif
}
void testUnicodeAtomicPublication()
{
    const auto path = fromUtf8Path(root + "原子 保存/材质.mat");
    check(PlatformFile::writeAllTextAtomic(path, "initial"),
          "atomic material save accepts physical Unicode directories");
    const std::string a(16384, 'A'), b(16384, 'B');
    bool first = false, second = false;
    std::thread one([&] { first = PlatformFile::writeAllTextAtomic(path, a); }),
        two([&] { second = PlatformFile::writeAllTextAtomic(path, b); });
    one.join();
    two.join();
    const auto text = PlatformFile::readAllText(path);
    check(first && second && (text == a || text == b),
          "concurrent atomic writes keep a complete valid file with separate temporary names");
}
void testCustomRootPackage()
{
    const auto project = std::filesystem::absolute(root + "CustomRootPackage");
    PlatformFile::writeAllText(project / "Content/Materials/child.mat",
                               "TINY_MAT 2\nparent \"assets/Materials/base.mat\"\n");
    PlatformFile::writeAllText(
        project / "Content/Materials/base.mat",
        "TINY_MAT 2\ndefinition \"builtin:PBR\"\ntexture \"BaseMap\" \"assets/Textures/white.ppm\"\n");
    PlatformFile::writeAllText(project / "Content/Textures/white.ppm", "P3\n1 1\n255\n255 255 255\n");
    check(PakArchive::packDirectory(project, project.parent_path() / "custom-root.tpak"),
          "pack non-assets logical asset root");
    ResourceManager resources;
    resources.initialize(nullptr, project / "Content");
    PlatformFile::clearMounts();
    PlatformFile::mountPak(project.parent_path() / "custom-root.tpak");
    PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    MaterialSystem system(resources);
    const auto resolved = system.resolve("assets/Materials/child.mat");
    const auto proxy = system.BuildProxy("assets/Materials/child.mat");
    check(resolved && std::any_of(proxy->textures.begin(), proxy->textures.end(),
                                  [](const auto& t) { return t.parameterName == "BaseMap" && t.resource; }),
          "custom-root mounted package resolves assets alias for child, parent and texture");
    PlatformFile::clearMounts();
    resources.shutdown();
}
void testFailedParentRepair()
{
    write("recover.matgraph", "TINY_MATGRAPH 999\n");
    write("recoverChild.mat", "TINY_MAT 2\ndefinition \"recover.matgraph\"\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    const auto error = system.BuildProxy("assets/recoverChild.mat");
    check(error && error->state.shadingModel == MaterialShadingModel::Unlit,
          "failed first descendant load installs error material");
    const auto result = system.applyDefinitionDraft("assets/recover.matgraph", makeBuiltinPbrDefinition());
    const auto repaired = system.BuildProxy("assets/recoverChild.mat");
    check(result.success && repaired != error && repaired->state.shadingModel == MaterialShadingModel::Lit,
          "parent-only repair recovers cached failed-first-load descendant");
    resources.shutdown();
}
void testNestedRootPackage()
{
    const auto project = std::filesystem::absolute(root + "NestedRootPackage");
    auto definition = makeBuiltinPbrDefinition();
    for (auto& p : definition.graph.GetParameters())
        if (p.name == "BaseMap")
            p.defaultValue = std::string("assets/Textures/white.ppm");
    PlatformFile::writeAllText(project / "Data/Content/Materials/parent.matgraph", definition.graph.serialize());
    PlatformFile::writeAllText(project / "Data/Content/Materials/base.mat",
                               "TINY_MAT 2\ndefinition \"Data/Content/Materials/parent.matgraph\"\n");
    PlatformFile::writeAllText(project / "Data/Content/Materials/child.mat",
                               "TINY_MAT 2\nparent \"assets/Materials/base.mat\"\n");
    PlatformFile::writeAllText(project / "Data/Content/Textures/white.ppm", "P3\n1 1\n255\n255 255 255\n");
    check(PakArchive::packDirectory(project, project.parent_path() / "nested-root.tpak"),
          "pack multi-level logical asset root");
    ResourceManager resources;
    resources.initialize(nullptr, project / "Data/Content", "Data/Content");
    PlatformFile::clearMounts();
    PlatformFile::mountPak(project.parent_path() / "nested-root.tpak");
    PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    MaterialSystem system(resources);
    const auto resolved = system.resolve("assets/Materials/child.mat");
    const auto proxy = system.BuildProxy("assets/Materials/child.mat");
    const bool loaded =
        resolved && std::any_of(proxy->textures.begin(), proxy->textures.end(),
                                [](const auto& t) { return t.parameterName == "BaseMap" && t.resource; });
    PlatformFile::clearMounts();
    resources.shutdown();
    check(loaded, "nested-root package preserves full configured prefix for aliases, parent and texture");
}
void testLegacyChildOfTypedParent()
{
    write("typedParent.mat", "TINY_MAT 2\ndefinition \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 1 0 0 1\n");
    write("legacyTypedChild.mat", "parent typedParent.mat\nroughness .2\noverride color 0\ncolor 0 1 0 1\n");
    write("mixedGraph.matgraph", makeBuiltinPbrDefinition().graph.serialize());
    write("legacyGraphChild.mat", "parent mixedGraph.matgraph\nroughness .4\n");
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    const auto child = system.resolve("assets/legacyTypedChild.mat");
    const auto value = [&](const char* name)
    {
        const auto& p = child->definition->parameters;
        const auto it = std::find_if(p.begin(), p.end(), [&](const auto& d) { return d.name == name; });
        return child->values[std::size_t(it - p.begin())];
    };
    check(child && std::get<Vec4>(value("BaseColor")).x == 1 && std::get<Vec4>(value("BaseColor")).y == 0 &&
              isClose(std::get<float>(value("Roughness")), .2f),
          "legacy child preserves typed parent values and explicit false override");
    const auto graphChild = system.resolve("assets/legacyGraphChild.mat");
    check(graphChild && graphChild->definition->sourcePath.find("mixedGraph.matgraph") != std::string::npos,
          "legacy child retains migrated graph parent definition");
    resources.shutdown();
}
void testCpuGenerationLifetime()
{
    auto definition = makeBuiltinPbrDefinition();
    definition.parameters.clear();
    definition.graph.GetParameters().clear();
    const auto constant = definition.graph.addNode(MaterialNodeType::Constant, "Color");
    const auto mask = definition.graph.addNode(MaterialNodeType::ComponentMask, "RGB");
    definition.graph.GetNodes().back().channelMask = "xyz";
    definition.graph.connect(constant, mask, 0);
    definition.graph.connect(mask, 1, 0);
    write("lifetime.matgraph", definition.graph.serialize());
    ResourceManager resources;
    resources.initialize(nullptr, root);
    MaterialSystem system(resources);
    auto proxy = system.BuildProxy("assets/lifetime.matgraph");
    std::weak_ptr<const CompiledMaterial> old = proxy->compiled;
    definition.graph.GetNodes()[1].value.x = .17f;
    check(system.applyDefinitionDraft("assets/lifetime.matgraph", definition).success,
          "publish a distinct constant generation");
    proxy.reset();
    check(old.expired(), "CPU source cache does not retain obsolete generations after snapshots release");
    resources.shutdown();
}
} // namespace
int main()
{
    std::cout.setf(std::ios::unitbuf);
    try
    {
        std::filesystem::create_directories(root);
        testLegacyMaterialCompatibility();
        testInvalidMaterialCandidates();
        testTypedMaterialRoundTrip();
        testThreeLevelInheritance();
        testParentCyclesAndDepth();
        testAssetRootPaths();
        testMaterialDependencies();
        testGraphTypes();
        testLegacyGraphUpgrade();
        testReachableCycles();
        testDeterministicCompilation();
        testLimits();
        testOpacityDependency();
        testDynamicIsolation();
        testReloadPublication();
        testCookingAndSourceCache();
        testMoveReferenceRollback();
        testUnicodeAtomicPublication();
        testCustomRootPackage();
        testNestedRootPackage();
        testFailedParentRepair();
        testLegacyChildOfTypedParent();
        testCpuGenerationLifetime();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] unhandled test error: " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
