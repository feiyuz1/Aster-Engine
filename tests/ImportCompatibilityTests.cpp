#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/Import/AssimpImporter.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace MiniEngine;

namespace
{
struct CompatibilityCase
{
    const char *name;
    std::filesystem::path relativeSource;
};

bool ContainsAsset(const ModelImportResult &result, const std::string &type)
{
    for (const auto &asset : result.assets)
        if (asset.type == type)
            return true;
    return false;
}
} // namespace

int main()
{
    const std::filesystem::path modelRoot = TINYENGINE_ASSIMP_MODEL_DIR;
    const std::vector<CompatibilityCase> cases = {{"fbx_skeleton_animation", "FBX/animation_with_skeleton.fbx"},
                                                  {"fbx_complex_multimesh", "FBX/spider.fbx"},
                                                  {"gltf2_skin", "glTF2/simple_skin/simple_skin.gltf"},
                                                  {"gltf2_complex_pbr", "glTF2/2CylinderEngine-glTF-Binary/2CylinderEngine.glb"}};
    const std::filesystem::path outputRoot = "Saved/ImportCompatibility";
    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    std::filesystem::create_directories(outputRoot, error);
    std::ostringstream report;
    report << "# Assimp FBX/glTF compatibility report\n\n"
              "| Case | Result | Meshes | Materials | Animations | Bones | Clips | Vertices | Triangles | Compression | Warnings |\n"
              "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    int failures = 0;
    for (const auto &test : cases)
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
        options.resourcePrefix = std::filesystem::path("Saved/ImportCompatibility") / test.name;
        const auto result = AssimpImporter::ImportModel(source, outputRoot / test.name, options);
        bool valid = result.success && ContainsAsset(result, "Mesh") && ContainsAsset(result, "Prefab") &&
                     result.compatibility.sourceMeshes > 0 && result.compatibility.importedMeshVertices > 0;
        if (result.compatibility.importedBones > 0)
            valid = valid && ContainsAsset(result, "Skeleton");
        if (result.compatibility.importedBones > 0 && result.compatibility.sourceAnimations > 0)
            valid = valid && result.compatibility.importedAnimationClips > 0 && ContainsAsset(result, "Animation");
        for (const auto &asset : result.assets)
        {
            valid = valid && std::filesystem::is_regular_file(asset.path);
            if (asset.type == "Animation")
            {
                SkeletalClip clip;
                valid = valid && LoadSkeletalClipAsset(asset.path, clip) && clip.IsCompressed();
            }
            else if (asset.type == "Skeleton")
            {
                Skeleton skeleton;
                valid = valid && LoadSkeletonAsset(asset.path, skeleton) && skeleton.bones.size() <= 64;
            }
            else if (asset.type == "Mesh")
            {
                const std::string text = PlatformFile::ReadAllText(asset.path);
                valid = valid && text.rfind("TINY_MESH 2", 0) == 0 && text.find("nan") == std::string::npos;
            }
        }
        if (!valid)
        {
            ++failures;
            std::cerr << test.name << " failed: " << result.error << '\n';
        }
        const auto &value = result.compatibility;
        report << "| " << test.name << " | " << (valid ? "PASS" : "FAIL") << " | " << value.sourceMeshes << " | " << value.sourceMaterials
               << " | " << value.sourceAnimations << " | " << value.importedBones << " | " << value.importedAnimationClips << " | "
               << value.importedMeshVertices << " | " << value.importedTriangles << " | " << value.animationCompressionRatio << " | "
               << result.warnings.size() << " |\n";
    }
    report << "\nThe suite validates FBX skeleton/animation, FBX multi-mesh hierarchy, glTF 2.0 skinning, and a large glTF PBR scene.\n";
    PlatformFile::WriteAllText(outputRoot / "REPORT.md", report.str());
    std::cout << report.str();
    return failures == 0 ? 0 : 1;
}
