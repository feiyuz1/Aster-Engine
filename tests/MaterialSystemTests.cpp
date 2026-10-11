#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>

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
    Check(PlatformFile::WriteAllText(root + name, text), "write material fixture");
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
}
int main()
{
    std::filesystem::create_directories(root);
    TestLegacyMaterialCompatibility();
    TestInvalidMaterialCandidates();
    TestTypedMaterialRoundTrip();
    return failures ? 1 : 0;
}
