#include "MiniEngine/Resource/AssetMaintenance.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/Resource.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::string Normalize(const std::filesystem::path &path)
{
    return path.lexically_normal().generic_string();
}

bool ReplaceAll(std::string &source, const std::string &from, const std::string &to)
{
    bool changed = false;
    std::size_t offset = 0;
    while (!from.empty() && (offset = source.find(from, offset)) != std::string::npos)
    {
        source.replace(offset, from.size(), to);
        offset += to.size();
        changed = true;
    }
    return changed;
}

bool SavePpm(const std::filesystem::path &path, const int size, const std::vector<std::uint8_t> &rgba)
{
    std::ostringstream output;
    output << "P3\n" << size << ' ' << size << "\n255\n";
    for (std::size_t index = 0; index < static_cast<std::size_t>(size) * size; ++index)
        output << static_cast<int>(rgba[index * 4]) << ' ' << static_cast<int>(rgba[index * 4 + 1]) << ' '
               << static_cast<int>(rgba[index * 4 + 2]) << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}

void PutPixel(std::vector<std::uint8_t> &pixels, const int size, const int x, const int y, const float depth,
              std::vector<float> &depthBuffer, const Vec3 &normal)
{
    if (x < 0 || y < 0 || x >= size || y >= size)
        return;
    const std::size_t index = static_cast<std::size_t>(y) * size + x;
    if (depth >= depthBuffer[index])
        return;
    depthBuffer[index] = depth;
    const float light = std::clamp(0.25f + 0.75f * std::abs(Dot(Normalize(normal), Normalize(Vec3{-0.4f, 0.8f, 0.5f}))), 0.0f, 1.0f);
    pixels[index * 4] = static_cast<std::uint8_t>(70.0f * light);
    pixels[index * 4 + 1] = static_cast<std::uint8_t>(150.0f * light);
    pixels[index * 4 + 2] = static_cast<std::uint8_t>(230.0f * light);
    pixels[index * 4 + 3] = 255;
}
} // namespace

std::vector<MissingAssetDependency> AssetMaintenance::FindMissingDependencies(const AssetRegistry &registry)
{
    const auto assets = registry.GetAssets();
    std::vector<MissingAssetDependency> result;
    for (const auto &asset : assets)
        for (const auto &dependency : asset.dependencies)
        {
            if (registry.Find(dependency) || std::filesystem::is_regular_file(dependency))
                continue;
            MissingAssetDependency issue{asset.path, dependency, {}};
            const std::filesystem::path missing = dependency;
            for (const auto &candidate : assets)
                if (std::filesystem::path(candidate.path).filename() == missing.filename() ||
                    std::filesystem::path(candidate.path).stem() == missing.stem())
                    issue.candidates.push_back(candidate.path);
            result.push_back(std::move(issue));
        }
    return result;
}

bool AssetMaintenance::RepairDependency(AssetRegistry &registry, const std::string &owner, const std::string &missingPath,
                                        const std::string &replacement, std::string *error)
{
    const auto info = registry.Find(owner);
    if (!info || (!registry.Find(replacement) && !std::filesystem::is_regular_file(replacement)))
    {
        if (error)
            *error = "Owner or replacement asset does not exist";
        return false;
    }
    std::string text = PlatformFile::ReadAllText(owner);
    if (!text.empty() && ReplaceAll(text, missingPath, replacement) && !PlatformFile::WriteAllText(owner, text))
    {
        if (error)
            *error = "Could not update owner file";
        return false;
    }
    auto dependencies = info->dependencies;
    for (auto &dependency : dependencies)
        if (Normalize(dependency) == Normalize(missingPath))
            dependency = Normalize(replacement);
    return registry.SetDependencies(owner, std::move(dependencies));
}

bool AssetMaintenance::MoveAsset(AssetRegistry &registry, const std::filesystem::path &source, const std::filesystem::path &destination,
                                 std::string *error)
{
    if (!std::filesystem::is_regular_file(source) || std::filesystem::exists(destination))
    {
        if (error)
            *error = "Source is missing or destination already exists";
        return false;
    }
    std::error_code code;
    std::filesystem::create_directories(destination.parent_path(), code);
    std::filesystem::rename(source, destination, code);
    if (code)
    {
        if (error)
            *error = code.message();
        return false;
    }
    const std::filesystem::path oldMeta = source.string() + ".meta", newMeta = destination.string() + ".meta";
    if (std::filesystem::is_regular_file(oldMeta))
        std::filesystem::rename(oldMeta, newMeta, code);
    const std::string oldPath = Normalize(source), newPath = Normalize(destination);
    for (const auto &asset : registry.GetAssets())
    {
        if (asset.path == oldPath)
            continue;
        std::string text = PlatformFile::ReadAllText(asset.path);
        if (!text.empty() && ReplaceAll(text, oldPath, newPath))
            PlatformFile::WriteAllText(asset.path, text);
        auto dependencies = asset.dependencies;
        bool changed = false;
        for (auto &dependency : dependencies)
            if (Normalize(dependency) == oldPath)
            {
                dependency = newPath;
                changed = true;
            }
        if (changed)
            registry.SetDependencies(asset.path, std::move(dependencies));
    }
    const std::filesystem::path root = source.parent_path().empty() ? "." : source.parent_path();
    registry.Scan(root, true);
    registry.Scan(destination.parent_path(), true);
    return true;
}

bool AssetMaintenance::GenerateThumbnail(const AssetInfo &asset, const std::filesystem::path &output, const int requestedSize,
                                         std::string *error)
{
    const int size = std::clamp(requestedSize, 32, 512);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4, 255);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const std::uint8_t shade = static_cast<std::uint8_t>(((x / 16 + y / 16) & 1) ? 42 : 58);
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
            pixels[index] = pixels[index + 1] = pixels[index + 2] = shade;
        }
    if (asset.type == "Texture")
    {
        TextureResource texture;
        if (texture.Load(asset.path) && texture.GetCompression() == TextureResource::Compression::None && !texture.IsHdr())
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const int sourceX = x * texture.GetWidth() / size, sourceY = y * texture.GetHeight() / size;
                    const std::size_t source = (static_cast<std::size_t>(sourceY) * texture.GetWidth() + sourceX) * 4;
                    const std::size_t destination = (static_cast<std::size_t>(y) * size + x) * 4;
                    std::copy_n(texture.GetData().data() + source, 4, pixels.data() + destination);
                }
    }
    else if (asset.type == "Material")
    {
        MaterialResource material;
        if (material.Load(asset.path))
        {
            const Vec4 color = material.GetBaseColor();
            for (int y = size / 8; y < size * 7 / 8; ++y)
                for (int x = size / 8; x < size * 7 / 8; ++x)
                {
                    const float nx = (x - size * 0.5f) / (size * 0.375f), ny = (y - size * 0.5f) / (size * 0.375f);
                    const float radius = nx * nx + ny * ny;
                    if (radius > 1.0f)
                        continue;
                    const float light = 0.25f + 0.75f * std::sqrt(1.0f - radius);
                    const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
                    pixels[index] = static_cast<std::uint8_t>(std::clamp(color.x * light, 0.0f, 1.0f) * 255.0f);
                    pixels[index + 1] = static_cast<std::uint8_t>(std::clamp(color.y * light, 0.0f, 1.0f) * 255.0f);
                    pixels[index + 2] = static_cast<std::uint8_t>(std::clamp(color.z * light, 0.0f, 1.0f) * 255.0f);
                }
        }
    }
    else if (asset.type == "Mesh")
    {
        MeshResource mesh;
        if (mesh.Load(asset.path) && !mesh.GetVertices().empty())
        {
            std::vector<float> depth(static_cast<std::size_t>(size) * size, std::numeric_limits<float>::max());
            const Aabb bounds = mesh.GetBounds();
            const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
            const float scale = 0.8f * size / std::max({bounds.maximum.x - bounds.minimum.x, bounds.maximum.y - bounds.minimum.y, 0.001f});
            for (const auto &vertex : mesh.GetVertices())
            {
                const int x = static_cast<int>((vertex.position.x - center.x) * scale + size * 0.5f);
                const int y = static_cast<int>((center.y - vertex.position.y) * scale + size * 0.5f);
                PutPixel(pixels, size, x, y, vertex.position.z, depth, vertex.normal);
            }
        }
    }
    if (!SavePpm(output, size, pixels))
    {
        if (error)
            *error = "Could not write thumbnail";
        return false;
    }
    return true;
}

bool AssetMaintenance::ChunkAsset(const std::filesystem::path &source, const std::filesystem::path &manifest,
                                  const std::size_t requestedChunkBytes, std::string *error)
{
    const auto bytes = PlatformFile::ReadAllBytes(source);
    if (bytes.empty())
    {
        if (error)
            *error = "Source is empty or unreadable";
        return false;
    }
    const std::size_t chunkBytes = std::max<std::size_t>(64 * 1024, requestedChunkBytes);
    std::filesystem::create_directories(manifest.parent_path());
    std::ostringstream description;
    description << "TINYCHUNK 1\nsize " << bytes.size() << "\nchunkSize " << chunkBytes << '\n';
    for (std::size_t offset = 0, index = 0; offset < bytes.size(); offset += chunkBytes, ++index)
    {
        std::ostringstream name;
        name << manifest.filename().string() << ".chunk" << std::setfill('0') << std::setw(4) << index;
        const std::size_t count = std::min(chunkBytes, bytes.size() - offset);
        std::vector<std::uint8_t> chunk(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        if (!PlatformFile::WriteAllBytes(manifest.parent_path() / name.str(), chunk))
        {
            if (error)
                *error = "Could not write chunk";
            return false;
        }
        description << "chunk " << std::quoted(name.str()) << ' ' << count << '\n';
    }
    return PlatformFile::WriteAllText(manifest, description.str());
}

bool ChunkedAssetStream::Open(const std::filesystem::path &manifest)
{
    m_chunks.clear();
    m_size = 0;
    m_chunkSize = 0;
    std::istringstream input(PlatformFile::ReadAllText(manifest));
    std::string signature, token;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINYCHUNK" || version != 1)
        return false;
    while (input >> token)
        if (token == "size")
            input >> m_size;
        else if (token == "chunkSize")
            input >> m_chunkSize;
        else if (token == "chunk")
        {
            std::string path;
            std::size_t ignored = 0;
            input >> std::quoted(path) >> ignored;
            m_chunks.push_back(std::move(path));
        }
    m_directory = manifest.parent_path();
    return m_size > 0 && m_chunkSize > 0 && !m_chunks.empty();
}

std::vector<std::uint8_t> ChunkedAssetStream::Read(const std::uint64_t offset, const std::size_t requested) const
{
    if (offset >= m_size || requested == 0 || m_chunkSize == 0)
        return {};
    const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(requested, m_size - offset));
    std::vector<std::uint8_t> result;
    result.reserve(count);
    std::uint64_t cursor = offset;
    while (result.size() < count)
    {
        const std::size_t chunkIndex = static_cast<std::size_t>(cursor / m_chunkSize);
        if (chunkIndex >= m_chunks.size())
            return {};
        const auto bytes = PlatformFile::ReadAllBytes(m_directory / m_chunks[chunkIndex]);
        const std::size_t local = static_cast<std::size_t>(cursor % m_chunkSize);
        if (local >= bytes.size())
            return {};
        const std::size_t amount = std::min(count - result.size(), bytes.size() - local);
        result.insert(result.end(), bytes.begin() + static_cast<std::ptrdiff_t>(local),
                      bytes.begin() + static_cast<std::ptrdiff_t>(local + amount));
        cursor += amount;
    }
    return result;
}

std::uint64_t ChunkedAssetStream::GetSize() const
{
    return m_size;
}
std::size_t ChunkedAssetStream::GetChunkSize() const
{
    return m_chunkSize;
}
} // namespace MiniEngine
