#include "MiniEngine/Build/AssetCompiler.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace MiniEngine
{
namespace
{
std::string Extension(const std::filesystem::path &path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

bool Copy(const std::filesystem::path &source, const std::filesystem::path &destination, std::string &message)
{
    std::error_code error;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
        return true;
    message = error.message();
    return false;
}
} // namespace

AssetCompileResult PlatformAssetCompiler::Compile(const std::filesystem::path &source, const std::filesystem::path &destination)
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
    if (IsTexture(source))
    {
        TextureResource texture;
        if (!texture.Load(source.generic_string()))
        {
            result.error = texture.GetError();
            return result;
        }
        writer.WriteBytes("TNYTEX1\0", 8);
        writer.WriteU32(static_cast<std::uint32_t>(texture.GetWidth()));
        writer.WriteU32(static_cast<std::uint32_t>(texture.GetHeight()));
        writer.WriteBool(texture.IsHdr());
        if (texture.IsHdr())
        {
            writer.WriteU64(static_cast<std::uint64_t>(texture.GetFloatData().size()));
            for (const float value : texture.GetFloatData())
                writer.WriteFloat(value);
        }
        else
        {
            writer.WriteU64(static_cast<std::uint64_t>(texture.GetData().size()));
            writer.WriteBytes(texture.GetData().data(), texture.GetData().size());
        }
        result.type = CompiledAssetType::Texture;
        result.success = writer.Save(destination);
    }
    else if (IsMesh(source))
    {
        MeshResource mesh;
        if (!mesh.Load(source.generic_string()))
        {
            result.error = mesh.GetError();
            return result;
        }
        writer.WriteBytes("TNYMSH1\0", 8);
        writer.WriteU64(static_cast<std::uint64_t>(mesh.GetVertices().size()));
        writer.WriteU64(static_cast<std::uint64_t>(mesh.GetIndices().size()));
        for (const auto &vertex : mesh.GetVertices())
        {
            writer.WriteFloat(vertex.position.x);
            writer.WriteFloat(vertex.position.y);
            writer.WriteFloat(vertex.position.z);
            writer.WriteFloat(vertex.normal.x);
            writer.WriteFloat(vertex.normal.y);
            writer.WriteFloat(vertex.normal.z);
            writer.WriteFloat(vertex.uv.x);
            writer.WriteFloat(vertex.uv.y);
            writer.WriteFloat(vertex.tangent.x);
            writer.WriteFloat(vertex.tangent.y);
            writer.WriteFloat(vertex.tangent.z);
        }
        for (const auto index : mesh.GetIndices())
            writer.WriteU32(index);
        result.type = CompiledAssetType::Mesh;
        result.success = writer.Save(destination);
    }
    else
        result.success = Copy(source, destination, result.error);
    if (!result.success && result.error.empty())
        result.error = "Cannot write compiled asset";
    if (result.success)
        result.outputBytes = std::filesystem::file_size(destination, error);
    return result;
}

bool PlatformAssetCompiler::IsTexture(const std::filesystem::path &path)
{
    const std::string extension = Extension(path);
    return extension == ".ppm" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tif" || extension == ".tiff" || extension == ".hdr";
}

bool PlatformAssetCompiler::IsMesh(const std::filesystem::path &path)
{
    return Extension(path) == ".mesh";
}
} // namespace MiniEngine
