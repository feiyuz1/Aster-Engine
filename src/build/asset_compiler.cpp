// 文件：src/build/asset_compiler.cpp
// 作用：把源资产转换为目标平台可直接加载的运行时格式。
// 约定：烘焙输出必须可重复，并且不能依赖编辑器进程内的临时状态。
// 类型：实现文件。

#include "tinyengine/build/asset_compiler.h"

#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/material_system.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/resource/resource.h"
#include "tinyengine/serialization/archive.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace tinyengine
{
namespace
{
std::string extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

bool copy(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& message)
{
    std::error_code error;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
        return true;
    message = error.message();
    return false;
}
} // namespace

// 根据目标平台选择纹理压缩与资源布局；相同输入和平台配置必须生成字节一致的输出。
AssetCompileResult PlatformAssetCompiler::compile(const std::filesystem::path& source,
                                                  const std::filesystem::path& destination,
                                                  const std::filesystem::path& materialAssetRoot)
{
    AssetCompileResult result;
    std::error_code error;
    result.sourceBytes = std::filesystem::file_size(source, error);
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
    {
        result.error = error.message();
        return result;
    }
    BinaryWriter writer;
    if (extension(source) == ".mat" || extension(source) == ".matgraph")
    {
        auto assetRoot = materialAssetRoot;
        if (assetRoot.empty())
        {
            assetRoot = std::filesystem::absolute(source).parent_path();
            for (auto parent = assetRoot; !parent.empty() && parent != parent.parent_path();
                 parent = parent.parent_path())
                if (parent.filename() == "assets")
                {
                    assetRoot = parent;
                    break;
                }
        }
        ResourceManager resources;
        resources.initialize(nullptr, assetRoot);
        MaterialSystem system(resources);
        const auto path = toUtf8GenericPath(std::filesystem::absolute(source).lexically_normal());
        const auto resolved = system.resolve(path);
        if (!resolved)
        {
            for (const auto& d : system.GetDiagnostics(path))
                result.error += d.message + '\n';
            return result;
        }
        const auto compiled = MaterialCompiler{}.compile(*resolved->definition);
        if (!compiled.Success())
        {
            for (const auto& d : compiled.diagnostics)
                result.error += d.message + '\n';
            return result;
        }
        if (!MaterialCompiler{}.writeCache(*compiled.material, ShaderPipeline::loadMaterialTemplates(),
                                           assetRoot.parent_path() / "saved/MaterialCache"))
        {
            result.error = "Unable to write generated material source cache";
            return result;
        }
        result.type = CompiledAssetType::Material;
        result.success = copy(source, destination, result.error);
        resources.shutdown();
    }
    else if (isTexture(source))
    {
        TextureResource texture;
        if (!texture.load(source.generic_string()))
        {
            result.error = texture.getError();
            return result;
        }
        writer.writeBytes("TNYTEX1\0", 8);
        writer.writeU32(static_cast<std::uint32_t>(texture.getWidth()));
        writer.writeU32(static_cast<std::uint32_t>(texture.getHeight()));
        writer.writeBool(texture.isHdr());
        if (texture.isHdr())
        {
            writer.writeU64(static_cast<std::uint64_t>(texture.GetFloatData().size()));
            for (const float value : texture.GetFloatData())
                writer.writeFloat(value);
        }
        else
        {
            writer.writeU64(static_cast<std::uint64_t>(texture.GetData().size()));
            writer.writeBytes(texture.GetData().data(), texture.GetData().size());
        }
        result.type = CompiledAssetType::Texture;
        result.success = writer.Save(destination);
    }
    else if (isMesh(source))
    {
        MeshResource mesh;
        if (!mesh.load(source.generic_string()))
        {
            result.error = mesh.getError();
            return result;
        }
        writer.writeBytes("TNYMSH2\0", 8);
        writer.writeU64(static_cast<std::uint64_t>(mesh.GetVertices().size()));
        writer.writeU64(static_cast<std::uint64_t>(mesh.GetIndices().size()));
        for (const auto& vertex : mesh.GetVertices())
        {
            writer.writeFloat(vertex.position.x);
            writer.writeFloat(vertex.position.y);
            writer.writeFloat(vertex.position.z);
            writer.writeFloat(vertex.normal.x);
            writer.writeFloat(vertex.normal.y);
            writer.writeFloat(vertex.normal.z);
            writer.writeFloat(vertex.uv.x);
            writer.writeFloat(vertex.uv.y);
            writer.writeFloat(vertex.tangent.x);
            writer.writeFloat(vertex.tangent.y);
            writer.writeFloat(vertex.tangent.z);
            for (const float value :
                 {vertex.boneIndices.x, vertex.boneIndices.y, vertex.boneIndices.z, vertex.boneIndices.w,
                  vertex.boneWeights.x, vertex.boneWeights.y, vertex.boneWeights.z, vertex.boneWeights.w})
                writer.writeFloat(value);
        }
        for (const auto index : mesh.GetIndices())
            writer.writeU32(index);
        result.type = CompiledAssetType::Mesh;
        result.success = writer.Save(destination);
    }
    else
        result.success = copy(source, destination, result.error);
    if (!result.success && result.error.empty())
        result.error = "Cannot write compiled asset";
    if (result.success)
        result.outputBytes = std::filesystem::file_size(destination, error);
    return result;
}

bool PlatformAssetCompiler::isTexture(const std::filesystem::path& path)
{
    const std::string fileExtension = extension(path);
    return fileExtension == ".ppm" || fileExtension == ".png" || fileExtension == ".jpg" || fileExtension == ".jpeg" ||
           fileExtension == ".bmp" || fileExtension == ".tif" || fileExtension == ".tiff" || fileExtension == ".hdr";
}

bool PlatformAssetCompiler::isMesh(const std::filesystem::path& path)
{
    return extension(path) == ".mesh";
}
} // namespace tinyengine
