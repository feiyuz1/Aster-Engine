#include "MiniEngine/Import/AssimpImporter.hpp"
#include "MiniEngine/Animation/SkeletalAnimation.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Task/JobSystem.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MiniEngine
{
namespace
{
std::string SafeName(std::string value, const std::string &fallback)
{
    for (char &character : value)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
            character = '_';
    while (!value.empty() && value.back() == '_')
        value.pop_back();
    return value.empty() ? fallback : value;
}

std::filesystem::path ResourcePath(const std::filesystem::path &file, const std::filesystem::path &outputDirectory,
                                   const ModelImportOptions &options)
{
    const auto relative = std::filesystem::relative(file, outputDirectory);
    return options.resourcePrefix.empty() ? file.lexically_normal() : (options.resourcePrefix / relative).lexically_normal();
}

bool WriteMesh(const aiMesh &mesh, const std::filesystem::path &path, const float scale,
               const std::unordered_map<std::string, unsigned int> &globalBones)
{
    std::ostringstream out;
    out << std::setprecision(9) << "TINY_MESH 2\n";
    for (unsigned int index = 0; index < mesh.mNumVertices; ++index)
    {
        const aiVector3D &value = mesh.mVertices[index];
        out << "v " << value.x * scale << ' ' << value.y * scale << ' ' << value.z * scale << '\n';
    }
    for (unsigned int index = 0; index < mesh.mNumVertices; ++index)
    {
        const aiVector3D value = mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][index] : aiVector3D{};
        out << "vt " << value.x << ' ' << value.y << '\n';
    }
    for (unsigned int index = 0; index < mesh.mNumVertices; ++index)
    {
        const aiVector3D value = mesh.HasNormals() ? mesh.mNormals[index] : aiVector3D{0, 0, 1};
        out << "vn " << value.x << ' ' << value.y << ' ' << value.z << '\n';
    }
    struct Skin
    {
        float indices[4]{};
        float weights[4]{};
    };
    std::vector<Skin> skin(mesh.mNumVertices);
    for (unsigned int boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex)
    {
        const auto globalBone = globalBones.find(mesh.mBones[boneIndex]->mName.C_Str());
        if (globalBone == globalBones.end() || globalBone->second >= 64)
            continue;
        for (unsigned int weightIndex = 0; weightIndex < mesh.mBones[boneIndex]->mNumWeights; ++weightIndex)
        {
            const auto &weight = mesh.mBones[boneIndex]->mWeights[weightIndex];
            if (weight.mVertexId >= skin.size())
                continue;
            auto &vertex = skin[weight.mVertexId];
            for (int slot = 0; slot < 4; ++slot)
                if (weight.mWeight > vertex.weights[slot])
                {
                    for (int move = 3; move > slot; --move)
                    {
                        vertex.weights[move] = vertex.weights[move - 1];
                        vertex.indices[move] = vertex.indices[move - 1];
                    }
                    vertex.weights[slot] = weight.mWeight;
                    vertex.indices[slot] = static_cast<float>(globalBone->second);
                    break;
                }
        }
    }
    for (unsigned int index = 0; index < mesh.mNumVertices; ++index)
    {
        const float total = skin[index].weights[0] + skin[index].weights[1] + skin[index].weights[2] + skin[index].weights[3];
        if (total > 0.0f)
            for (float &weight : skin[index].weights)
                weight /= total;
        out << "vw " << index + 1;
        for (int slot = 0; slot < 4; ++slot)
            out << ' ' << skin[index].indices[slot] << ' ' << skin[index].weights[slot];
        out << '\n';
    }
    for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
    {
        const aiFace &face = mesh.mFaces[faceIndex];
        if (face.mNumIndices != 3)
            continue;
        out << "f";
        for (unsigned int corner = 0; corner < 3; ++corner)
        {
            const unsigned int index = face.mIndices[corner] + 1;
            out << ' ' << index << '/' << index << '/' << index;
        }
        out << '\n';
    }
    return PlatformFile::WriteAllText(path, out.str());
}

bool CopyTexture(const aiScene &scene, const std::filesystem::path &source, const aiString &textureName,
                 const std::filesystem::path &outputDirectory, std::filesystem::path &copied)
{
    const std::string name = textureName.C_Str();
    if (name.empty())
        return false;
    if (name.front() == '*')
    {
        const aiTexture *texture = scene.GetEmbeddedTexture(name.c_str());
        if (!texture)
            return false;
        if (texture->mHeight == 0)
        {
            std::string extension = texture->achFormatHint;
            if (extension.empty())
                extension = "bin";
            copied = outputDirectory / ("embedded_" + name.substr(1) + "." + extension);
            const auto *begin = reinterpret_cast<const std::uint8_t *>(texture->pcData);
            return PlatformFile::WriteAllBytes(copied, std::vector<std::uint8_t>(begin, begin + texture->mWidth));
        }
        copied = outputDirectory / ("embedded_" + name.substr(1) + ".ppm");
        std::ostringstream ppm;
        ppm << "P3\n" << texture->mWidth << ' ' << texture->mHeight << "\n255\n";
        for (unsigned int index = 0; index < texture->mWidth * texture->mHeight; ++index)
        {
            const aiTexel &pixel = texture->pcData[index];
            ppm << static_cast<unsigned int>(pixel.r) << ' ' << static_cast<unsigned int>(pixel.g) << ' '
                << static_cast<unsigned int>(pixel.b) << '\n';
        }
        return PlatformFile::WriteAllText(copied, ppm.str());
    }
    const std::filesystem::path textureSource = (source.parent_path() / std::filesystem::path(name)).lexically_normal();
    if (!std::filesystem::is_regular_file(textureSource))
        return false;
    copied = outputDirectory / textureSource.filename();
    std::error_code error;
    std::filesystem::copy_file(textureSource, copied, std::filesystem::copy_options::overwrite_existing, error);
    return !error;
}

bool WriteMaterial(const aiScene &scene, const aiMaterial &material, const std::filesystem::path &source, const std::filesystem::path &path,
                   const std::filesystem::path &outputDirectory, const ModelImportOptions &options, ModelImportResult &result)
{
    aiColor4D color{0.7f, 0.7f, 0.7f, 1.0f};
    if (aiGetMaterialColor(&material, AI_MATKEY_BASE_COLOR, &color) != AI_SUCCESS)
        aiGetMaterialColor(&material, AI_MATKEY_COLOR_DIFFUSE, &color);
    float opacity = color.a;
    material.Get(AI_MATKEY_OPACITY, opacity);
    color.a = opacity;
    std::ostringstream out;
    out << std::setprecision(7) << "color " << color.r << ' ' << color.g << ' ' << color.b << ' ' << color.a << '\n';
    float metallic = 0.0f, roughness = 0.7f;
    material.Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    aiColor3D emissive{};
    material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
    out << "metallic " << std::clamp(metallic, 0.0f, 1.0f) << "\nroughness " << std::clamp(roughness, 0.04f, 1.0f) << "\nemissive "
        << emissive.r << ' ' << emissive.g << ' ' << emissive.b << '\n';
    if (color.a < 0.999f)
        out << "blend alpha\n";
    auto importTexture = [&](const aiTextureType type, const char *key, const aiTextureType fallback = aiTextureType_NONE) {
        aiString textureName;
        if (material.GetTexture(type, 0, &textureName) != AI_SUCCESS && fallback != aiTextureType_NONE)
            material.GetTexture(fallback, 0, &textureName);
        std::filesystem::path copied;
        if (!CopyTexture(scene, source, textureName, outputDirectory, copied))
            return;
        result.assets.push_back({"TextureSource", copied});
        std::string extension = copied.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".ppm" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
            extension == ".tif" || extension == ".tiff" || extension == ".hdr")
            out << key << ' ' << ResourcePath(copied, outputDirectory, options).generic_string() << '\n';
        else
            result.warnings.push_back("Copied texture but runtime decoder does not support it: " + copied.generic_string());
    };
    importTexture(aiTextureType_BASE_COLOR, "texture", aiTextureType_DIFFUSE);
    importTexture(aiTextureType_NORMALS, "normalTexture", aiTextureType_HEIGHT);
    importTexture(aiTextureType_METALNESS, "metallicTexture");
    importTexture(aiTextureType_DIFFUSE_ROUGHNESS, "roughnessTexture");
    importTexture(aiTextureType_AMBIENT_OCCLUSION, "aoTexture", aiTextureType_LIGHTMAP);
    importTexture(aiTextureType_EMISSIVE, "emissiveTexture");
    return PlatformFile::WriteAllText(path, out.str());
}

Vec3 RadiansToDegrees(const aiVector3D &value)
{
    return {value.x * 180.0f / Pi, value.y * 180.0f / Pi, value.z * 180.0f / Pi};
}

aiVector3D SampleVectorKeys(const aiVectorKey *keys, const unsigned int count, const double time, const aiVector3D &fallback)
{
    if (!keys || count == 0)
        return fallback;
    unsigned int right = 0;
    while (right < count && keys[right].mTime < time)
        ++right;
    if (right == 0)
        return keys[0].mValue;
    if (right == count)
        return keys[count - 1].mValue;
    const auto &leftKey = keys[right - 1], &rightKey = keys[right];
    const float alpha =
        rightKey.mTime > leftKey.mTime ? static_cast<float>((time - leftKey.mTime) / (rightKey.mTime - leftKey.mTime)) : 0.0f;
    return leftKey.mValue + (rightKey.mValue - leftKey.mValue) * alpha;
}

aiQuaternion SampleRotationKeys(const aiQuatKey *keys, const unsigned int count, const double time, const aiQuaternion &fallback)
{
    if (!keys || count == 0)
        return fallback;
    unsigned int right = 0;
    while (right < count && keys[right].mTime < time)
        ++right;
    if (right == 0)
        return keys[0].mValue;
    if (right == count)
        return keys[count - 1].mValue;
    aiQuaternion result;
    const auto &leftKey = keys[right - 1], &rightKey = keys[right];
    const float alpha =
        rightKey.mTime > leftKey.mTime ? static_cast<float>((time - leftKey.mTime) / (rightKey.mTime - leftKey.mTime)) : 0.0f;
    aiQuaternion::Interpolate(result, leftKey.mValue, rightKey.mValue, alpha);
    return result.Normalize();
}

Vec3 QuaternionToEulerDegrees(const aiQuaternion &value)
{
    const float sinX = 2.0f * (value.w * value.x + value.y * value.z);
    const float cosX = 1.0f - 2.0f * (value.x * value.x + value.y * value.y);
    const float sinY = std::clamp(2.0f * (value.w * value.y - value.z * value.x), -1.0f, 1.0f);
    const float sinZ = 2.0f * (value.w * value.z + value.x * value.y);
    const float cosZ = 1.0f - 2.0f * (value.y * value.y + value.z * value.z);
    return {std::atan2(sinX, cosX) * 180.0f / Pi, std::asin(sinY) * 180.0f / Pi, std::atan2(sinZ, cosZ) * 180.0f / Pi};
}
} // namespace

bool AssimpImporter::CanImport(const std::filesystem::path &source)
{
    Assimp::Importer importer;
    return importer.IsExtensionSupported(source.extension().string());
}

std::vector<std::string> AssimpImporter::GetSupportedExtensions()
{
    Assimp::Importer importer;
    aiString extensions;
    importer.GetExtensionList(extensions);
    std::vector<std::string> result;
    std::istringstream stream(extensions.C_Str());
    std::string extension;
    while (std::getline(stream, extension, ';'))
    {
        if (extension.rfind("*.", 0) == 0)
            extension.erase(0, 1);
        if (!extension.empty())
            result.push_back(extension);
    }
    return result;
}

ModelImportResult AssimpImporter::ImportModel(const std::filesystem::path &source, const std::filesystem::path &outputDirectory,
                                              const ModelImportOptions &options)
{
    ModelImportResult result;
    if (!std::filesystem::is_regular_file(source))
    {
        result.error = "Source model does not exist: " + source.generic_string();
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(outputDirectory, error);
    if (error)
    {
        result.error = "Cannot create output directory: " + error.message();
        return result;
    }

    unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
                         aiProcess_ValidateDataStructure | aiProcess_SortByPType | aiProcess_CalcTangentSpace;
    if (options.generateSmoothNormals)
        flags |= aiProcess_GenSmoothNormals;
    if (options.flipUvs)
        flags |= aiProcess_FlipUVs;
    if (options.optimizeMeshes)
        flags |= aiProcess_OptimizeMeshes;
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(source.string(), flags);
    if (!scene || !scene->mRootNode || scene->mNumMeshes == 0)
    {
        result.error = importer.GetErrorString();
        return result;
    }
    result.compatibility.sourceMeshes = scene->mNumMeshes;
    result.compatibility.sourceMaterials = scene->mNumMaterials;
    result.compatibility.sourceAnimations = scene->mNumAnimations;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        result.compatibility.importedMeshVertices += scene->mMeshes[meshIndex]->mNumVertices;
        result.compatibility.importedTriangles += scene->mMeshes[meshIndex]->mNumFaces;
    }

    const std::string baseName = SafeName(source.stem().string(), "Model");
    std::unordered_set<std::string> boneNames;
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        for (unsigned int boneIndex = 0; boneIndex < scene->mMeshes[meshIndex]->mNumBones; ++boneIndex)
            boneNames.insert(scene->mMeshes[meshIndex]->mBones[boneIndex]->mName.C_Str());
    std::unordered_map<std::string, unsigned int> globalBones;
    std::function<void(const aiNode &)> indexBones = [&](const aiNode &node) {
        const std::string name = node.mName.C_Str();
        if (boneNames.count(name) && globalBones.size() < 64)
            globalBones[name] = static_cast<unsigned int>(globalBones.size());
        for (unsigned int index = 0; index < node.mNumChildren; ++index)
            indexBones(*node.mChildren[index]);
    };
    indexBones(*scene->mRootNode);
    result.compatibility.importedBones = globalBones.size();
    result.compatibility.truncatedBoneCount = boneNames.size() > globalBones.size() ? boneNames.size() - globalBones.size() : 0;
    if (result.compatibility.truncatedBoneCount > 0)
        result.warnings.push_back("Skeleton exceeded the 64 bone GPU limit; " + std::to_string(result.compatibility.truncatedBoneCount) +
                                  " bones were omitted");
    std::filesystem::path skeletonPath;
    Skeleton importedSkeleton;
    if (!globalBones.empty())
    {
        Skeleton &skeleton = importedSkeleton;
        skeleton.bones.resize(globalBones.size());
        std::unordered_map<std::string, Mat4> inverseBinds;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
            for (unsigned int boneIndex = 0; boneIndex < scene->mMeshes[meshIndex]->mNumBones; ++boneIndex)
            {
                const aiBone &sourceBone = *scene->mMeshes[meshIndex]->mBones[boneIndex];
                Mat4 matrix;
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        matrix.At(row, column) = sourceBone.mOffsetMatrix[row][column];
                inverseBinds[sourceBone.mName.C_Str()] = matrix;
            }
        for (const auto &entry : globalBones)
        {
            Bone &bone = skeleton.bones[entry.second];
            bone.name = entry.first;
            bone.inverseBind = inverseBinds.count(entry.first) ? inverseBinds.at(entry.first) : Mat4::Identity();
            const aiNode *node = scene->mRootNode->FindNode(entry.first.c_str());
            for (node = node ? node->mParent : nullptr; node; node = node->mParent)
                if (const auto parent = globalBones.find(node->mName.C_Str()); parent != globalBones.end())
                {
                    bone.parent = static_cast<int>(parent->second);
                    break;
                }
        }
        skeletonPath = outputDirectory / (baseName + ".skeleton");
        if (!SaveSkeletonAsset(skeleton, skeletonPath))
        {
            result.error = "Failed to write skeleton";
            return result;
        }
        result.assets.push_back({"Skeleton", skeletonPath});
    }
    std::vector<std::filesystem::path> skeletalClipPaths;
    if (!importedSkeleton.bones.empty())
        for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
        {
            const aiAnimation &animation = *scene->mAnimations[animationIndex];
            const double ticksPerSecond = animation.mTicksPerSecond > 0.0 ? animation.mTicksPerSecond : 25.0;
            SkeletalClip clip;
            clip.name = SafeName(animation.mName.C_Str(), "Animation" + std::to_string(animationIndex));
            clip.duration = static_cast<float>(animation.mDuration / ticksPerSecond);
            clip.looping = true;
            std::unordered_map<std::string, const aiNodeAnim *> channels;
            for (unsigned int channelIndex = 0; channelIndex < animation.mNumChannels; ++channelIndex)
                channels[animation.mChannels[channelIndex]->mNodeName.C_Str()] = animation.mChannels[channelIndex];
            const unsigned int frameCount = std::max(2u, static_cast<unsigned int>(std::ceil(clip.duration * 30.0f)) + 1u);
            for (unsigned int frame = 0; frame < frameCount; ++frame)
            {
                const float seconds = std::min(clip.duration, static_cast<float>(frame) / 30.0f);
                SkeletalKeyframe key;
                key.time = seconds;
                key.localPose.resize(importedSkeleton.bones.size());
                for (std::size_t boneIndex = 0; boneIndex < importedSkeleton.bones.size(); ++boneIndex)
                {
                    const Bone &bone = importedSkeleton.bones[boneIndex];
                    const aiNode *node = scene->mRootNode->FindNode(bone.name.c_str());
                    aiVector3D scale{1, 1, 1}, position{};
                    aiQuaternion rotation;
                    if (node)
                        node->mTransformation.Decompose(scale, rotation, position);
                    if (const auto channel = channels.find(bone.name); channel != channels.end())
                    {
                        const double ticks = seconds * ticksPerSecond;
                        position = SampleVectorKeys(channel->second->mPositionKeys, channel->second->mNumPositionKeys, ticks, position);
                        scale = SampleVectorKeys(channel->second->mScalingKeys, channel->second->mNumScalingKeys, ticks, scale);
                        rotation = SampleRotationKeys(channel->second->mRotationKeys, channel->second->mNumRotationKeys, ticks, rotation);
                    }
                    key.localPose[boneIndex] = {{position.x * options.scale, position.y * options.scale, position.z * options.scale},
                                                QuaternionToEulerDegrees(rotation),
                                                {scale.x, scale.y, scale.z}};
                }
                clip.keyframes.push_back(std::move(key));
            }
            if (clip.duration <= 0.0f)
            {
                result.warnings.push_back("Skipped zero-duration animation: " + clip.name);
                continue;
            }
            if (options.compressAnimations)
            {
                const auto stats = clip.Compress(
                    {options.animationPositionTolerance, options.animationRotationToleranceDegrees, options.animationScaleTolerance});
                result.compatibility.animationCompressionRatio =
                    result.compatibility.importedAnimationClips == 0
                        ? stats.ratio
                        : (result.compatibility.animationCompressionRatio * result.compatibility.importedAnimationClips + stats.ratio) /
                              static_cast<float>(result.compatibility.importedAnimationClips + 1);
            }
            const auto clipPath = outputDirectory / (baseName + "_" + clip.name + ".skanim");
            if (SaveSkeletalClipAsset(clip, clipPath))
            {
                skeletalClipPaths.push_back(clipPath);
                result.assets.push_back({"Animation", clipPath});
                ++result.compatibility.importedAnimationClips;
            }
            else
                result.warnings.push_back("Failed to serialize animation: " + clip.name);
        }
    std::vector<std::filesystem::path> materialPaths(scene->mNumMaterials);
    for (unsigned int index = 0; index < scene->mNumMaterials; ++index)
    {
        aiString materialName;
        scene->mMaterials[index]->Get(AI_MATKEY_NAME, materialName);
        const auto path =
            outputDirectory / (baseName + "_Mat" + std::to_string(index) + "_" + SafeName(materialName.C_Str(), "Material") + ".mat");
        if (!WriteMaterial(*scene, *scene->mMaterials[index], source, path, outputDirectory, options, result))
        {
            result.error = "Failed to write material: " + path.generic_string();
            return result;
        }
        materialPaths[index] = path;
        result.assets.push_back({"Material", path});
    }

    std::vector<std::filesystem::path> meshPaths(scene->mNumMeshes);
    for (unsigned int index = 0; index < scene->mNumMeshes; ++index)
    {
        const aiMesh &mesh = *scene->mMeshes[index];
        const auto path =
            outputDirectory / (baseName + "_Mesh" + std::to_string(index) + "_" + SafeName(mesh.mName.C_Str(), "Mesh") + ".mesh");
        if (!WriteMesh(mesh, path, options.scale, globalBones))
        {
            result.error = "Failed to write mesh: " + path.generic_string();
            return result;
        }
        meshPaths[index] = path;
        result.assets.push_back({"Mesh", path});
    }

    World prefabWorld;
    std::function<void(const aiNode &, Entity)> importNode = [&](const aiNode &node, const Entity parent) {
        const Entity entity = prefabWorld.CreateEntity(SafeName(node.mName.C_Str(), "Node"));
        if (parent != InvalidEntity)
            prefabWorld.SetParent(entity, parent);
        aiVector3D scale, rotation, position;
        node.mTransformation.Decompose(scale, rotation, position);
        if (auto *transform = prefabWorld.GetComponent<TransformComponent>(entity))
        {
            transform->position = {position.x * options.scale, position.y * options.scale, position.z * options.scale};
            transform->rotation = RadiansToDegrees(rotation);
            transform->scale = {scale.x, scale.y, scale.z};
        }
        for (unsigned int localIndex = 0; localIndex < node.mNumMeshes; ++localIndex)
        {
            const unsigned int meshIndex = node.mMeshes[localIndex];
            const aiMesh &mesh = *scene->mMeshes[meshIndex];
            Entity target = entity;
            if (localIndex > 0)
            {
                target = prefabWorld.CreateEntity(SafeName(mesh.mName.C_Str(), "Mesh"));
                prefabWorld.SetParent(target, entity);
            }
            const auto material = mesh.mMaterialIndex < materialPaths.size() ? materialPaths[mesh.mMaterialIndex] : std::filesystem::path{};
            prefabWorld.AddComponent(
                target,
                MeshComponent{ResourcePath(meshPaths[meshIndex], outputDirectory, options).generic_string(),
                              material.empty() ? std::string{} : ResourcePath(material, outputDirectory, options).generic_string()});
            if (!skeletonPath.empty() && !skeletalClipPaths.empty())
            {
                AnimationComponent animation;
                animation.clipPath = ResourcePath(skeletalClipPaths.front(), outputDirectory, options).generic_string();
                animation.skeletonPath = ResourcePath(skeletonPath, outputDirectory, options).generic_string();
                prefabWorld.AddComponent(target, std::move(animation));
            }
        }
        for (unsigned int index = 0; index < node.mNumChildren; ++index)
            importNode(*node.mChildren[index], entity);
    };
    importNode(*scene->mRootNode, InvalidEntity);
    result.prefabPath = outputDirectory / (baseName + ".prefab");
    if (!SceneSerializer::Save(prefabWorld, result.prefabPath))
    {
        result.error = "Failed to write prefab";
        return result;
    }
    result.assets.push_back({"Prefab", result.prefabPath});
    AssetRegistry registry;
    registry.Scan(outputDirectory);
    std::vector<std::string> dependencyIds;
    for (const auto &asset : result.assets)
    {
        if (asset.path == result.prefabPath)
            continue;
        if (const auto info = registry.Find(asset.path.generic_string()))
            dependencyIds.push_back(info->id);
    }
    registry.SetDependencies(result.prefabPath.generic_string(), std::move(dependencyIds));
    registry.SetImportSetting(result.prefabPath.generic_string(), "source",
                              std::filesystem::absolute(source).lexically_normal().generic_string());
    registry.SetImportSetting(result.prefabPath.generic_string(), "scale", std::to_string(options.scale));
    registry.SetImportSetting(result.prefabPath.generic_string(), "flipUvs", options.flipUvs ? "true" : "false");
    registry.SetImportSetting(result.prefabPath.generic_string(), "optimizeMeshes", options.optimizeMeshes ? "true" : "false");
    registry.SetImportSetting(result.prefabPath.generic_string(), "compressAnimations", options.compressAnimations ? "true" : "false");
    registry.SetImportSetting(result.prefabPath.generic_string(), "animationPositionTolerance",
                              std::to_string(options.animationPositionTolerance));
    registry.SetImportSetting(result.prefabPath.generic_string(), "animationRotationToleranceDegrees",
                              std::to_string(options.animationRotationToleranceDegrees));
    registry.SetImportSetting(result.prefabPath.generic_string(), "animationScaleTolerance",
                              std::to_string(options.animationScaleTolerance));
    for (const auto &asset : result.assets)
        registry.MarkImported(asset.path.generic_string());
    result.success = true;
    return result;
}

struct AssetImportQueue::TaskState
{
    std::uint64_t id = 0;
    std::atomic<AssetImportStatus> status{AssetImportStatus::Queued};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    mutable std::mutex mutex;
    ModelImportResult result;
};

void AssetImportQueue::Initialize(JobSystem *jobs)
{
    m_jobs = jobs;
}

std::uint64_t AssetImportQueue::Submit(std::filesystem::path source, std::filesystem::path outputDirectory, ModelImportOptions options)
{
    auto task = std::make_shared<TaskState>();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        task->id = m_nextId++;
        m_tasks[task->id] = task;
    }
    auto work = [task, source = std::move(source), outputDirectory = std::move(outputDirectory), options = std::move(options)]() {
        if (task->cancel.load())
        {
            task->status = AssetImportStatus::Cancelled;
            return;
        }
        task->status = AssetImportStatus::Running;
        task->progress = 0.1f;
        ModelImportResult result = AssimpImporter::ImportModel(source, outputDirectory, options);
        task->progress = 0.9f;
        const bool success = result.success;
        {
            std::lock_guard<std::mutex> lock(task->mutex);
            task->result = std::move(result);
        }
        if (task->cancel.load())
            task->status = AssetImportStatus::Cancelled;
        else
            task->status = success ? AssetImportStatus::Completed : AssetImportStatus::Failed;
        task->progress = 1.0f;
    };
    if (m_jobs)
    {
        auto job = m_jobs->CreateJob(std::move(work), JobPriority::Normal);
        m_jobs->Submit(job);
    }
    else
        work();
    return task->id;
}

bool AssetImportQueue::Cancel(const std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_tasks.find(id);
    if (found == m_tasks.end())
        return false;
    const auto status = found->second->status.load();
    if (status == AssetImportStatus::Completed || status == AssetImportStatus::Failed || status == AssetImportStatus::Cancelled)
        return false;
    found->second->cancel = true;
    return true;
}

AssetImportSnapshot AssetImportQueue::Get(const std::uint64_t id) const
{
    std::shared_ptr<TaskState> task;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_tasks.find(id);
        if (found == m_tasks.end())
            return {};
        task = found->second;
    }
    AssetImportSnapshot snapshot;
    snapshot.id = id;
    snapshot.status = task->status.load();
    snapshot.progress = task->progress.load();
    std::lock_guard<std::mutex> lock(task->mutex);
    snapshot.result = task->result;
    return snapshot;
}

void AssetImportQueue::ClearFinished()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto iterator = m_tasks.begin(); iterator != m_tasks.end();)
    {
        const auto status = iterator->second->status.load();
        if (status == AssetImportStatus::Completed || status == AssetImportStatus::Failed || status == AssetImportStatus::Cancelled)
            iterator = m_tasks.erase(iterator);
        else
            ++iterator;
    }
}
} // namespace MiniEngine
