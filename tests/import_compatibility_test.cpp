// 文件：tests/import_compatibility_test.cpp
// 作用：使用真实模型样本验证 Assimp 导入兼容性矩阵。
// 约定：测试必须可重复执行，不依赖执行顺序或开发机上的隐式状态。
// 类型：实现文件。

#include "tinyengine/animation/skeletal_animation.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/import/assimp_importer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/asset_registry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace tinyengine;

namespace
{
struct CompatibilityCase
{
    const char* name;
    std::filesystem::path relativeSource;
};

bool containsAsset(const ModelImportResult& result, const std::string& type)
{
    for (const auto& asset : result.assets)
        if (asset.type == type)
            return true;
    return false;
}
// A self-contained glTF keeps animation regressions independent of Assimp's optional sample checkout.
// At bind: Root = (1, 0, 2), Tip = (1, 2, 5). The unweighted Helper moves +2 along Y over one second.
bool writeSkeletalWorkflowFixture(const std::filesystem::path& directory, const bool incompatibleBinds = false)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::vector<std::uint8_t> bytes;
    const auto floats = [&bytes](std::initializer_list<float> values)
    {
        for (const float value : values)
        {
            const auto offset = bytes.size();
            bytes.resize(offset + sizeof(value));
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }
    };
    floats({0, 0, 0, 1, 0, 0, 0, 1, 0}); // Positions, 36 bytes.
    for (const std::uint16_t value : {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0})
    {
        const auto offset = bytes.size();
        bytes.resize(offset + sizeof(value));
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    floats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}); // Weights, offset 60.
    floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, 0,  -2, 1,
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, -2, -5, 1}); // Inverse binds, offset 108.
    floats({0, 1});                                              // Times, offset 236.
    floats({0, 2, 0, 0, 4, 0});                                  // Helper translations, offset 244.
    std::string gltf = R"({
 "asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,4,5]}],
 "nodes":[
  {"name":"Armature","translation":[0,0,2],"children":[1]},
  {"name":"Root","translation":[1,0,0],"children":[2]},
  {"name":"Helper","translation":[0,2,0],"children":[3]},
  {"name":"Tip","translation":[0,0,3]},
  {"name":"Skinned","mesh":0,"skin":0},
  {"name":"Static","mesh":1}],
 "skins":[{"inverseBindMatrices":3,"joints":[1,3],"skeleton":1}],
 "meshes":[
  {"name":"Weighted","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]},
  {"name":"Static","primitives":[{"attributes":{"POSITION":0}}]}],
 "animations":[
  {"name":"Walk/A","samplers":[{"input":4,"output":5}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]},
  {"name":"Walk:A","samplers":[{"input":4,"output":5}],"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}}]}],
 "buffers":[{"uri":"rig.bin","byteLength":268}],
 "bufferViews":[
  {"buffer":0,"byteOffset":0,"byteLength":36},
  {"buffer":0,"byteOffset":36,"byteLength":24},
  {"buffer":0,"byteOffset":60,"byteLength":48},
  {"buffer":0,"byteOffset":108,"byteLength":128},
  {"buffer":0,"byteOffset":236,"byteLength":8},
  {"buffer":0,"byteOffset":244,"byteLength":24}],
 "accessors":[
  {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
  {"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},
  {"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
  {"bufferView":3,"componentType":5126,"count":2,"type":"MAT4"},
  {"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
  {"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"}]
})";
    if (incompatibleBinds)
    {
        floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, -2, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, -2, -5, 1});
        const auto replace = [&gltf](const std::string& from, const std::string& to)
        {
            const auto position = gltf.find(from);
            if (position == std::string::npos)
                return false;
            gltf.replace(position, from.size(), to);
            return true;
        };
        if (!replace(R"("name":"Static","mesh":1)", R"("name":"Static","mesh":1,"skin":1)") ||
            !replace(
                R"("inverseBindMatrices":3,"joints":[1,3],"skeleton":1}])",
                R"("inverseBindMatrices":3,"joints":[1,3],"skeleton":1},{"inverseBindMatrices":6,"joints":[1,3],"skeleton":1}])") ||
            !replace(R"("name":"Static","primitives":[{"attributes":{"POSITION":0}}])",
                     R"("name":"Static","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}])") ||
            !replace(R"("byteLength":268)", R"("byteLength":396)") ||
            !replace(
                R"("buffer":0,"byteOffset":244,"byteLength":24}])",
                R"("buffer":0,"byteOffset":244,"byteLength":24},{"buffer":0,"byteOffset":268,"byteLength":128}])") ||
            !replace(
                R"("bufferView":5,"componentType":5126,"count":2,"type":"VEC3"}])",
                R"("bufferView":5,"componentType":5126,"count":2,"type":"VEC3"},{"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"}])"))
            return false;
    }
    return !error && PlatformFile::writeAllBytes(directory / "rig.bin", bytes) &&
           PlatformFile::writeAllText(directory / "rig.gltf", gltf);
}

int skeletalWorkflowRegression(const std::filesystem::path& outputRoot)
{
    int failures = 0;
    const auto check = [&failures](const bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Skeletal workflow: " << message << '\n';
            ++failures;
        }
    };
    const auto fixtureDirectory = outputRoot / "SkeletalFixture";
    check(writeSkeletalWorkflowFixture(fixtureDirectory), "write generated glTF fixture");
    for (const float scale : {1.0f, 2.0f})
    {
        const auto output = outputRoot / (scale == 1.0f ? "RigUnit" : "RigScaled");
        ModelImportOptions options;
        options.scale = scale;
        options.optimizeMeshes = false;
        options.compressAnimations = scale == 2.0f;
        options.resourcePrefix = output;
        const auto result = AssimpImporter::importModel(fixtureDirectory / "rig.gltf", output, options);
        check(result.success, "import generated glTF: " + result.error);
        if (!result.success)
            continue;
        Skeleton skeleton;
        std::vector<std::filesystem::path> clips;
        for (const auto& asset : result.assets)
            if (asset.type == "Skeleton")
                check(loadSkeletonAsset(asset.path, skeleton), "load imported skeleton");
            else if (asset.type == "Animation")
                clips.push_back(asset.path);
        check(skeleton.bones.size() == 2, "retain the two deforming joints without losing auxiliary transforms");
        check(clips.size() == 2 && std::set<std::filesystem::path>(clips.begin(), clips.end()).size() == 2,
              "source animation names that sanitize alike must produce distinct files");
        const int tip = skeleton.findBone("Tip");
        check(tip >= 0, "preserve Tip bone name");
        for (const auto& path : clips)
        {
            SkeletalClip clip;
            check(loadSkeletalClipAsset(path, clip), "load generated animation");
            const auto bind = BuildSkinMatrices(skeleton, clip.sample(0.0f, skeleton.bones.size(), false));
            bool identity = bind.size() == 2;
            for (const auto& matrix : bind)
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        identity =
                            identity && std::abs(matrix.at(row, column) - (row == column ? 1.0f : 0.0f)) < 0.001f;
            check(identity, "bind pose cancels inverse bind at import scale " + std::to_string(scale));
            const auto half = BuildSkinMatrices(skeleton, clip.sample(0.5f, skeleton.bones.size(), false));
            check(tip >= 0 && static_cast<std::size_t>(tip) < half.size() &&
                      std::abs(half[tip].at(1, 3) - scale) < 0.001f,
                  "animation of an unweighted ancestor reaches its deforming child at scale " + std::to_string(scale));
        }
        World prefab;
        check(SceneSerializer::load(prefab, result.prefabPath), "load mixed static/skinned prefab");
        int animatedMeshes = 0, staticMeshes = 0;
        prefab.GetComponentManager().ForEach<MeshComponent>(
            [&](const Entity entity, MeshComponent&)
            {
                if (prefab.GetComponent<AnimationComponent>(entity))
                    ++animatedMeshes;
                else
                    ++staticMeshes;
            });
        check(animatedMeshes == 1 && staticMeshes == 1, "only weighted meshes receive AnimationComponent");
        AssetRegistry registry;
        registry.scan(output, false);
        std::unordered_map<std::string, AssetInfo> before;
        for (const auto& path : clips)
        {
            const auto info = registry.Find(path.generic_string());
            check(info.has_value(), "animation is registered");
            if (!info)
                continue;
            before.emplace(path.generic_string(), *info);
            const auto skeletonSetting = info->importSettings.find("preview.skeleton");
            const auto meshSetting = info->importSettings.find("preview.mesh");
            const auto rig = skeletonSetting == info->importSettings.end() ? std::optional<AssetInfo>{}
                                                                           : registry.FindById(skeletonSetting->second);
            const auto mesh = meshSetting == info->importSettings.end() ? std::optional<AssetInfo>{}
                                                                        : registry.FindById(meshSetting->second);
            check(rig && rig->type == "Skeleton" && mesh && mesh->type == "Mesh",
                  "preview skeleton and mesh metadata resolve through stable asset GUIDs");
            check(rig && mesh && info->dependencies.size() == 2 &&
                      std::find(info->dependencies.begin(), info->dependencies.end(), rig->id) !=
                          info->dependencies.end() &&
                      std::find(info->dependencies.begin(), info->dependencies.end(), mesh->id) !=
                          info->dependencies.end(),
                  "clip dependencies contain the skeleton and weighted mesh only");
            check(info->importSettings.count("source.animation.index") &&
                      info->importSettings.count("source.animation.name"),
                  "animation metadata preserves source animation identity");
        }
        const auto reimported = AssimpImporter::importModel(fixtureDirectory / "rig.gltf", output, options);
        check(reimported.success, "reimport generated glTF");
        registry.scan(output, false);
        for (const auto& entry : before)
        {
            const auto after = registry.Find(entry.first);
            check(after && after->id == entry.second.id && after->dependencies == entry.second.dependencies &&
                      after->importSettings == entry.second.importSettings,
                  "reimport preserves clip GUID and preview associations");
        }
    }
    const auto incompatibleDirectory = outputRoot / "IncompatibleBindFixture";
    check(writeSkeletalWorkflowFixture(incompatibleDirectory, true), "write incompatible bind-space fixture");
    ModelImportOptions incompatibleOptions;
    incompatibleOptions.optimizeMeshes = false;
    const auto incompatible = AssimpImporter::importModel(incompatibleDirectory / "rig.gltf",
                                                          outputRoot / "IncompatibleBindOutput", incompatibleOptions);
    check(!incompatible.success && incompatible.error.find("incompatible inverse bind") != std::string::npos,
          "reject meshes with incompatible shared-skeleton inverse bind matrices");
    return failures;
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path modelRoot = TINYENGINE_ASSIMP_MODEL_DIR;
    const std::vector<CompatibilityCase> cases = {
        {"fbx_skeleton_animation", "FBX/animation_with_skeleton.fbx"},
        {"fbx_complex_multimesh", "FBX/spider.fbx"},
        {"gltf2_skin", "glTF2/simple_skin/simple_skin.gltf"},
        {"gltf2_complex_pbr", "glTF2/2CylinderEngine-glTF-Binary/2CylinderEngine.glb"}};
    const std::filesystem::path outputRoot = "saved/ImportCompatibility";
    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    std::filesystem::create_directories(outputRoot, error);
    std::ostringstream report;
    report << "# Assimp FBX/glTF compatibility report\n\n"
              "| Case | Result | Meshes | Materials | Animations | Bones | Clips | Vertices | Triangles | Compression "
              "| Warnings |\n"
              "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    int failures = skeletalWorkflowRegression(outputRoot);
    if (argc > 1 && std::string(argv[1]) == "--skeletal-workflow")
        return failures == 0 ? 0 : 1;
    for (const auto& test : cases)
    {
        const auto source = modelRoot / test.relativeSource;
        if (!std::filesystem::is_regular_file(source))
        {
            std::cerr << "Missing compatibility fixture: " << source << '\n';
            ++failures;
            continue;
        }
        ModelImportOptions options;
        options.compressAnimations = true;
        options.resourcePrefix = std::filesystem::path("saved/ImportCompatibility") / test.name;
        const auto result = AssimpImporter::importModel(source, outputRoot / test.name, options);
        bool valid = result.success && containsAsset(result, "Mesh") && containsAsset(result, "Prefab") &&
                     result.compatibility.sourceMeshes > 0 && result.compatibility.importedMeshVertices > 0;
        if (result.compatibility.importedBones > 0)
            valid = valid && containsAsset(result, "Skeleton");
        if (result.compatibility.importedBones > 0 && result.compatibility.sourceAnimations > 0)
            valid = valid && result.compatibility.importedAnimationClips > 0 && containsAsset(result, "Animation");
        for (const auto& asset : result.assets)
        {
            valid = valid && std::filesystem::is_regular_file(asset.path);
            if (asset.type == "Animation")
            {
                SkeletalClip clip;
                valid = valid && loadSkeletalClipAsset(asset.path, clip) && clip.isCompressed();
            }
            else if (asset.type == "Skeleton")
            {
                Skeleton skeleton;
                valid = valid && loadSkeletonAsset(asset.path, skeleton) && skeleton.bones.size() <= 64;
            }
            else if (asset.type == "Mesh")
            {
                const std::string text = PlatformFile::readAllText(asset.path);
                valid = valid && text.rfind("TINY_MESH 2", 0) == 0 && text.find("nan") == std::string::npos;
            }
        }
        if (!valid)
        {
            ++failures;
            std::cerr << test.name << " failed: " << result.error << '\n';
        }
        const auto& value = result.compatibility;
        report << "| " << test.name << " | " << (valid ? "PASS" : "FAIL") << " | " << value.sourceMeshes << " | "
               << value.sourceMaterials << " | " << value.sourceAnimations << " | " << value.importedBones << " | "
               << value.importedAnimationClips << " | " << value.importedMeshVertices << " | "
               << value.importedTriangles << " | " << value.animationCompressionRatio << " | " << result.warnings.size()
               << " |\n";
    }
    report << "\nThe suite validates FBX skeleton/animation, FBX multi-mesh hierarchy, glTF 2.0 skinning, and a large "
              "glTF PBR scene.\n";
    PlatformFile::writeAllText(outputRoot / "REPORT.md", report.str());
    std::cout << report.str();
    return failures == 0 ? 0 : 1;
}
