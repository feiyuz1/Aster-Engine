#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/MaterialSystem.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <algorithm>

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
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] unhandled test error: " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
