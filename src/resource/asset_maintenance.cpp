// 文件：src/resource/asset_maintenance.cpp
// 作用：执行资源重命名、迁移、引用更新、缺失依赖检测和修复。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "tinyengine/resource/asset_maintenance.h"

#include "tinyengine/core/config.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/project/project.h"
#include "tinyengine/resource/material_asset_references.h"
#include "tinyengine/resource/resource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace tinyengine
{
namespace
{
std::string normalizePath(const std::filesystem::path& path)
{
    return toUtf8GenericPath(path.lexically_normal());
}

bool replaceAll(std::string& source, const std::string& from, const std::string& to)
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

std::filesystem::path absoluteAssetPath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::string referenceKey(const std::filesystem::path& path)
{
    auto key = normalizePath(path);
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

bool isPortablePath(const std::filesystem::path& path)
{
    return !path.empty() && !path.is_absolute() && *path.begin() != "..";
}

bool isShaderSource(const std::filesystem::path& path)
{
    auto extension = toUtf8GenericPath(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".glsl" || extension == ".vert" || extension == ".frag" || extension == ".geom" ||
           extension == ".tesc" || extension == ".tese" || extension == ".comp" || extension == ".inc" ||
           extension == ".hlsl";
}

bool rewriteProjectConfig(std::string& text, const std::filesystem::path& source,
                          const std::filesystem::path& destination, const std::filesystem::path& assetRoot,
                          const std::filesystem::path& projectRoot)
{
    bool changed = false;
    std::size_t lineStart = 0;
    while (lineStart < text.size())
    {
        auto lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        const auto keyStart = text.find_first_not_of(" \t\r", lineStart);
        const auto equals = text.find('=', lineStart);
        if (keyStart < lineEnd && equals < lineEnd && equals > keyStart && text[keyStart] != '#' &&
            text[keyStart] != ';')
        {
            const auto keyEnd = text.find_last_not_of(" \t", equals - 1);
            const auto key = text.substr(keyStart, keyEnd - keyStart + 1);
            if (key == "project.startupScene" || key == "input.bindings" || key == "render.environment")
            {
                const auto valueStart = text.find_first_not_of(" \t\r", equals + 1);
                const auto valueEnd = text.find_last_not_of(" \t\r", lineEnd - 1);
                if (valueStart < lineEnd && valueEnd >= valueStart)
                {
                    auto reference = text.substr(valueStart, valueEnd - valueStart + 1);
                    if (AssetMaintenance::rewriteReference(reference, source, destination, assetRoot, projectRoot))
                    {
                        const auto previousLength = valueEnd - valueStart + 1;
                        text.replace(valueStart, previousLength, reference);
                        lineEnd = lineEnd - previousLength + reference.size();
                        changed = true;
                    }
                }
            }
        }
        lineStart = lineEnd + 1;
    }
    return changed;
}

bool isReferenceToken(const std::string& format, const std::string& field, std::size_t index)
{
    if (format == "TINYSCENE")
        return ((field == "mesh" || field == "lod") && index < 2) ||
               ((field == "collider" || field == "audio") && index == 0) || (field == "animation" && index < 4);
    if (format == "TINY_BLENDTREE")
        return field == "sample" && index == 0;
    if (format == "TINY_ANIMGRAPH")
        return field == "state" && index == 1;
    if (format == "TINY_VFX")
        return field == "texture" && index == 0;
    if (format == "TINY_ENV")
        return (field == "sky" || field == "irradiance" || field == "prefilter" || field == "brdf") && index == 0;
    if (format.rfind("TINY", 0) == 0)
        return false;
    return index == 0 &&
           (field == "path" || field == "resource" || field == "mesh" || field == "material" || field == "texture" ||
            field == "shader" || field == "clip" || field == "skeleton" || field == "graph");
}

bool savePpm(const std::filesystem::path& path, const int size, const std::vector<std::uint8_t>& rgba)
{
    std::ostringstream output;
    output << "P3\n" << size << ' ' << size << "\n255\n";
    for (std::size_t index = 0; index < static_cast<std::size_t>(size) * size; ++index)
        output << static_cast<int>(rgba[index * 4]) << ' ' << static_cast<int>(rgba[index * 4 + 1]) << ' '
               << static_cast<int>(rgba[index * 4 + 2]) << '\n';
    return PlatformFile::writeAllText(path, output.str());
}

void PutPixel(std::vector<std::uint8_t>& pixels, const int size, const int x, const int y, const float depth,
              std::vector<float>& depthBuffer, const Vec3& normal, const Vec4& color = {0.28f, 0.59f, 0.9f, 1.0f})
{
    if (x < 0 || y < 0 || x >= size || y >= size)
        return;
    const std::size_t index = static_cast<std::size_t>(y) * size + x;
    if (depth >= depthBuffer[index])
        return;
    depthBuffer[index] = depth;
    const float light =
        std::clamp(0.25f + 0.75f * std::abs(dot(normalize(normal), normalize(Vec3{-0.4f, 0.8f, 0.5f}))), 0.0f, 1.0f);
    pixels[index * 4] = static_cast<std::uint8_t>(std::clamp(color.x * light, 0.0f, 1.0f) * 255.0f);
    pixels[index * 4 + 1] = static_cast<std::uint8_t>(std::clamp(color.y * light, 0.0f, 1.0f) * 255.0f);
    pixels[index * 4 + 2] = static_cast<std::uint8_t>(std::clamp(color.z * light, 0.0f, 1.0f) * 255.0f);
    pixels[index * 4 + 3] = 255;
}

struct ThumbnailVertex
{
    Vec3 position;
    Vec3 normal;
    Vec4 color;
};

Vec3 transformThumbnailPoint(const Mat4& matrix, const Vec3& point)
{
    return {matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3),
            matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3),
            matrix.at(2, 0) * point.x + matrix.at(2, 1) * point.y + matrix.at(2, 2) * point.z + matrix.at(2, 3)};
}

Vec3 transformThumbnailNormal(const Mat4& matrix, const Vec3& normal)
{
    return normalize({matrix.at(0, 0) * normal.x + matrix.at(0, 1) * normal.y + matrix.at(0, 2) * normal.z,
                      matrix.at(1, 0) * normal.x + matrix.at(1, 1) * normal.y + matrix.at(1, 2) * normal.z,
                      matrix.at(2, 0) * normal.x + matrix.at(2, 1) * normal.y + matrix.at(2, 2) * normal.z});
}

void appendThumbnailMesh(const MeshResource& mesh, const Mat4& transform, const Vec4& color,
                         std::vector<ThumbnailVertex>& vertices)
{
    vertices.reserve(vertices.size() + mesh.GetVertices().size());
    for (const auto& vertex : mesh.GetVertices())
        vertices.push_back({transformThumbnailPoint(transform, vertex.position),
                            transformThumbnailNormal(transform, vertex.normal), color});
}

void renderThumbnailVertices(const std::vector<ThumbnailVertex>& vertices, std::vector<std::uint8_t>& pixels,
                             const int size)
{
    if (vertices.empty())
        return;
    const Vec3 right = normalize(Vec3{0.82f, 0.0f, -0.57f});
    const Vec3 up = normalize(Vec3{-0.24f, 0.91f, -0.34f});
    const Vec3 forward = normalize(cross(right, up));
    float minimumX = std::numeric_limits<float>::max(), minimumY = minimumX;
    float maximumX = std::numeric_limits<float>::lowest(), maximumY = maximumX;
    for (const auto& vertex : vertices)
    {
        const float x = dot(vertex.position, right), y = dot(vertex.position, up);
        minimumX = std::min(minimumX, x);
        minimumY = std::min(minimumY, y);
        maximumX = std::max(maximumX, x);
        maximumY = std::max(maximumY, y);
    }
    const float scale = size * 0.82f / std::max({maximumX - minimumX, maximumY - minimumY, 0.001f});
    const float centerX = (minimumX + maximumX) * 0.5f, centerY = (minimumY + maximumY) * 0.5f;
    std::vector<float> depth(static_cast<std::size_t>(size) * size, std::numeric_limits<float>::max());
    for (const auto& vertex : vertices)
    {
        const int x = static_cast<int>((dot(vertex.position, right) - centerX) * scale + size * 0.5f);
        const int y = static_cast<int>((centerY - dot(vertex.position, up)) * scale + size * 0.5f);
        PutPixel(pixels, size, x, y, dot(vertex.position, forward), depth, vertex.normal, vertex.color);
        PutPixel(pixels, size, x + 1, y, dot(vertex.position, forward), depth, vertex.normal, vertex.color);
        PutPixel(pixels, size, x, y + 1, dot(vertex.position, forward), depth, vertex.normal, vertex.color);
    }
}
} // namespace

bool AssetMaintenance::rewriteReference(std::string& reference, const std::filesystem::path& source,
                                        const std::filesystem::path& destination,
                                        const std::filesystem::path& assetRoot,
                                        const std::filesystem::path& projectRoot)
{
    if (reference.empty() || source.empty() || destination.empty() || reference.rfind("builtin:", 0) == 0)
        return false;
    const auto sourceAbsolute = absoluteAssetPath(source), destinationAbsolute = absoluteAssetPath(destination);
    const auto root = absoluteAssetPath(assetRoot);
    const auto project = projectRoot.empty() ? root.parent_path() : absoluteAssetPath(projectRoot);
    const auto key = referenceKey(fromUtf8Path(reference));
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> replacements = {
        {source, destination}, {sourceAbsolute, destinationAbsolute}};
    const auto addRelative = [&](const std::filesystem::path& base, const std::filesystem::path& prefix = {})
    {
        const auto oldRelative = sourceAbsolute.lexically_relative(base);
        const auto newRelative = destinationAbsolute.lexically_relative(base);
        if (isPortablePath(oldRelative))
            replacements.emplace_back(prefix / oldRelative,
                                      isPortablePath(newRelative) ? prefix / newRelative : destinationAbsolute);
    };
    addRelative(project);
    addRelative(root);
    addRelative(root, root.filename());
    addRelative(root, "assets");
    for (const auto& replacement : replacements)
        if (key == referenceKey(replacement.first))
        {
            const auto rewritten = normalizePath(replacement.second);
            if (reference == rewritten)
                return false;
            reference = rewritten;
            return true;
        }
    return false;
}

bool AssetMaintenance::rewriteReferences(std::string& text, const std::filesystem::path& source,
                                         const std::filesystem::path& destination,
                                         const std::filesystem::path& assetRoot,
                                         const std::filesystem::path& projectRoot)
{
    std::istringstream header(text);
    std::string format;
    header >> format;
    bool changed = false;
    std::string output;
    std::size_t lineStart = 0;
    while (lineStart < text.size())
    {
        auto lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        std::istringstream fields(line);
        std::string field;
        fields >> field;
        struct Token
        {
            std::size_t start, length;
            std::string value;
        };
        std::vector<Token> tokens;
        std::size_t cursor = 0;
        while ((cursor = line.find('"', cursor)) != std::string::npos)
        {
            const auto start = cursor++;
            std::string value;
            bool closed = false;
            while (cursor < line.size())
            {
                const char c = line[cursor++];
                if (c == '"')
                {
                    closed = true;
                    break;
                }
                if (c == '\\' && cursor < line.size())
                    value += line[cursor++];
                else
                    value += c;
            }
            if (!closed)
                break;
            tokens.push_back({start, cursor - start, std::move(value)});
        }
        for (std::size_t index = tokens.size(); index > 0; --index)
        {
            auto& token = tokens[index - 1];
            if (!isReferenceToken(format, field, index - 1) ||
                !rewriteReference(token.value, source, destination, assetRoot, projectRoot))
                continue;
            std::ostringstream quoted;
            quoted << std::quoted(token.value);
            line.replace(token.start, token.length, quoted.str());
            changed = true;
        }
        // Legacy plain-text asset formats sometimes leave their single reference unquoted.
        if (tokens.empty() && isReferenceToken(format, field, 0))
        {
            std::string reference;
            if (fields >> reference && rewriteReference(reference, source, destination, assetRoot, projectRoot))
            {
                const auto start = line.find_first_not_of(" \t", line.find(field) + field.size());
                if (start != std::string::npos)
                {
                    auto end = line.find_first_of(" \t\r", start);
                    if (end == std::string::npos)
                        end = line.size();
                    line.replace(start, end - start, reference);
                    changed = true;
                }
            }
        }
        output += line;
        if (lineEnd < text.size())
            output += '\n';
        lineStart = lineEnd + 1;
    }
    if (changed)
        text = std::move(output);
    return changed;
}

bool AssetMaintenance::rewriteShaderIncludes(std::string& text, const std::filesystem::path& owner,
                                             const std::filesystem::path& newOwner, const std::filesystem::path& source,
                                             const std::filesystem::path& destination)
{
    const auto originalOwner = absoluteAssetPath(owner), targetOwner = absoluteAssetPath(newOwner);
    const bool ownerFolderMoved = referenceKey(originalOwner.parent_path()) != referenceKey(targetOwner.parent_path());
    const auto sourceKey = referenceKey(absoluteAssetPath(source));
    const auto target = absoluteAssetPath(destination);
    bool blockComment = false, changed = false;
    std::string output;
    std::size_t lineStart = 0;
    while (lineStart < text.size())
    {
        auto lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart), code(line.size(), ' ');
        bool quoted = false;
        for (std::size_t index = 0; index < line.size(); ++index)
        {
            if (blockComment)
            {
                if (line[index] == '*' && index + 1 < line.size() && line[index + 1] == '/')
                {
                    blockComment = false;
                    ++index;
                }
                continue;
            }
            if (quoted)
            {
                code[index] = line[index];
                if (line[index] == '\\' && index + 1 < line.size())
                {
                    ++index;
                    code[index] = line[index];
                }
                else if (line[index] == '"')
                    quoted = false;
                continue;
            }
            if (line[index] == '/' && index + 1 < line.size())
            {
                if (line[index + 1] == '/')
                    break;
                if (line[index + 1] == '*')
                {
                    blockComment = true;
                    ++index;
                    continue;
                }
            }
            code[index] = line[index];
            if (line[index] == '"')
                quoted = true;
        }
        const auto directive = code.find_first_not_of(" \t\r");
        if (directive != std::string::npos && code.compare(directive, 8, "#include") == 0 &&
            (directive + 8 == code.size() || std::isspace(static_cast<unsigned char>(code[directive + 8])) ||
             code[directive + 8] == '"'))
        {
            const auto first = code.find('"', directive + 8);
            const auto last = first == std::string::npos ? std::string::npos : code.find('"', first + 1);
            if (last != std::string::npos)
            {
                const auto previous = line.substr(first + 1, last - first - 1);
                const auto include = fromUtf8Path(previous);
                auto resolved = absoluteAssetPath(originalOwner.parent_path() / include);
                const bool includedAssetMoved = referenceKey(resolved) == sourceKey;
                if (includedAssetMoved)
                    resolved = target;
                if (includedAssetMoved || ownerFolderMoved)
                {
                    const auto relative = resolved.lexically_relative(targetOwner.parent_path());
                    const auto replacement =
                        normalizePath(include.is_absolute() || relative.empty() ? resolved : relative);
                    if (previous != replacement)
                    {
                        line.replace(first + 1, last - first - 1, replacement);
                        changed = true;
                    }
                }
            }
        }
        output += line;
        if (lineEnd < text.size())
            output += '\n';
        lineStart = lineEnd + 1;
    }
    if (changed)
        text = std::move(output);
    return changed;
}

std::vector<MissingAssetDependency> AssetMaintenance::FindMissingDependencies(const AssetRegistry& registry)
{
    const auto assets = registry.GetAssets();
    std::vector<MissingAssetDependency> result;
    for (const auto& asset : assets)
        for (const auto& dependency : asset.dependencies)
        {
            // Meta 依赖既可以是稳定 GUID，也可以是路径；两种形式都是正式引用。
            if (registry.Find(dependency) || registry.FindById(dependency) ||
                std::filesystem::is_regular_file(fromUtf8Path(dependency)))
                continue;
            MissingAssetDependency issue{asset.path, dependency, {}};
            const std::filesystem::path missing = dependency;
            for (const auto& candidate : assets)
                if (std::filesystem::path(candidate.path).filename() == missing.filename() ||
                    std::filesystem::path(candidate.path).stem() == missing.stem())
                    issue.candidates.push_back(candidate.path);
            result.push_back(std::move(issue));
        }
    return result;
}

bool AssetMaintenance::repairDependency(AssetRegistry& registry, const std::string& owner,
                                        const std::string& missingPath, const std::string& replacement,
                                        std::string* error)
{
    const auto info = registry.Find(owner);
    if (!info || (!registry.Find(replacement) && !std::filesystem::is_regular_file(replacement)))
    {
        if (error)
            *error = "Owner or replacement asset does not exist";
        return false;
    }
    std::string text = PlatformFile::readAllText(owner);
    if (!text.empty() && replaceAll(text, missingPath, replacement) && !PlatformFile::writeAllText(owner, text))
    {
        if (error)
            *error = "Could not update owner file";
        return false;
    }
    auto dependencies = info->dependencies;
    for (auto& dependency : dependencies)
        if (normalizePath(dependency) == normalizePath(missingPath))
            dependency = normalizePath(replacement);
    return registry.setDependencies(owner, std::move(dependencies));
}

// 先验证资产与 meta 的目标路径；meta 迁移失败时恢复源资产。
bool AssetMaintenance::moveAsset(AssetRegistry& registry, const std::filesystem::path& source,
                                 const std::filesystem::path& destination, std::string* error,
                                 const std::filesystem::path& projectRoot, const std::filesystem::path& projectFile)
{
    if (error)
        error->clear();
    auto oldMeta = source, newMeta = destination;
    oldMeta += ".meta";
    newMeta += ".meta";
    if (!std::filesystem::is_regular_file(source) || std::filesystem::exists(destination) ||
        std::filesystem::exists(newMeta) ||
        (std::filesystem::exists(oldMeta) && !std::filesystem::is_regular_file(oldMeta)))
    {
        if (error)
            *error = "Source is missing, source metadata is invalid, or destination asset/metadata already exists";
        return false;
    }
    std::error_code code;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), code);
    if (!code)
        std::filesystem::rename(source, destination, code);
    if (code)
    {
        if (error)
            *error = code.message();
        return false;
    }
    if (std::filesystem::is_regular_file(oldMeta))
    {
        std::filesystem::rename(oldMeta, newMeta, code);
        if (code)
        {
            const auto metadataError = code.message();
            std::filesystem::rename(destination, source, code);
            if (error)
                *error = "Could not move metadata: " + metadataError +
                         (code ? "; could not restore source asset: " + code.message() : "");
            return false;
        }
    }
    const std::string oldPath = normalizePath(source), newPath = normalizePath(destination);
    std::vector<std::filesystem::path> rewrittenAssets;
    struct Original
    {
        std::filesystem::path path;
        std::string text;
        bool existed = true;
    };
    std::vector<Original> originals;
    const auto scope = registry.GetRootPath();
    const auto rollback = [&](const std::string& message)
    {
        bool restored = true;
        std::error_code failure;
        for (auto it = originals.rbegin(); it != originals.rend(); ++it)
        {
            if (it->existed)
                restored = PlatformFile::writeAllTextAtomic(it->path, it->text) && restored;
            else
            {
                std::filesystem::remove(it->path, failure);
                restored = restored && !failure;
            }
        }
        if (std::filesystem::is_regular_file(newMeta))
        {
            std::filesystem::rename(newMeta, oldMeta, failure);
            restored = restored && !failure;
        }
        std::filesystem::rename(destination, source, failure);
        restored = restored && !failure;
        if (!scope.empty())
            registry.scan(scope);
        if (error)
            *error = message + (restored ? "" : "; rollback could not restore all files");
        return false;
    };
    const auto write = [&](const std::filesystem::path& path, const std::string& text)
    {
        const auto previous = PlatformFile::readAllText(path);
        if (!PlatformFile::writeAllTextAtomic(path, text))
            return false;
        originals.push_back({path, previous, true});
        return true;
    };
    if (source.extension() == ".mat" || source.extension() == ".matgraph")
    {
        auto text = PlatformFile::readAllText(destination);
        if (rewriteMaterialReferences(text, source.extension() == ".matgraph", source, destination,
                                      registry.GetRootPath(), oldPath, newPath))
            if (!write(destination, text))
                return rollback("Could not rewrite moved material references");
    }
    else
    {
        auto text = PlatformFile::readAllText(destination);
        bool changed = rewriteReferences(text, source, destination, scope, projectRoot);
        if (isShaderSource(source))
            changed = rewriteShaderIncludes(text, source, destination, source, destination) || changed;
        if (changed && !write(destination, text))
            return rollback("Could not rewrite moved asset references");
    }
    if (std::filesystem::is_regular_file(newMeta))
    {
        Config metadata;
        if (!metadata.load(toUtf8GenericPath(newMeta)))
            return rollback("Could not read moved metadata");
        bool changed = false;
        for (int index = 0; index < metadata.getInt("dependencies.count", 0); ++index)
        {
            const auto key = "dependencies." + std::to_string(index);
            auto dependency = metadata.getString(key);
            if (!registry.FindById(dependency) && rewriteReference(dependency, source, destination, scope, projectRoot))
            {
                metadata.set(key, std::move(dependency));
                changed = true;
            }
        }
        if (changed)
        {
            std::ostringstream text;
            for (const auto& entry : metadata.GetValues())
                text << entry.first << '=' << entry.second << '\n';
            if (!write(newMeta, text.str()))
                return rollback("Could not rewrite moved dependency metadata");
        }
    }
    std::vector<std::pair<std::string, std::vector<std::string>>> dependencyUpdates;
    for (const auto& asset : registry.GetAssets())
    {
        if (asset.path == oldPath)
            continue;
        std::string text = PlatformFile::readAllText(fromUtf8Path(asset.path));
        bool changedText =
            asset.type == "Material" || asset.type == "MaterialGraph"
                ? rewriteMaterialReferences(text, asset.type == "MaterialGraph", fromUtf8Path(asset.path),
                                            fromUtf8Path(asset.path), registry.GetRootPath(), oldPath, newPath)
                : rewriteReferences(text, source, destination, scope, projectRoot);
        if (isShaderSource(fromUtf8Path(asset.path)))
            changedText =
                rewriteShaderIncludes(text, fromUtf8Path(asset.path), fromUtf8Path(asset.path), source, destination) ||
                changedText;
        if (!text.empty() && changedText)
        {
            if (!write(fromUtf8Path(asset.path), text))
                return rollback("Could not rewrite asset references: " + asset.path);
            rewrittenAssets.emplace_back(fromUtf8Path(asset.path));
        }
        auto dependencies = asset.dependencies;
        bool changed = false;
        for (auto& dependency : dependencies)
            if (!registry.FindById(dependency))
                changed = rewriteReference(dependency, source, destination, scope, projectRoot) || changed;
        if (changed)
            dependencyUpdates.emplace_back(asset.path, std::move(dependencies));
    }
    for (auto& update : dependencyUpdates)
    {
        const auto meta = fromUtf8Path(update.first + ".meta");
        originals.push_back({meta, PlatformFile::readAllText(meta), std::filesystem::exists(meta)});
        if (!registry.setDependencies(update.first, std::move(update.second)))
            return rollback("Could not save dependency metadata: " + update.first);
    }
    if (!projectFile.empty() && std::filesystem::exists(projectFile))
    {
        if (!std::filesystem::is_regular_file(projectFile))
            return rollback("Project configuration is not a regular file");
        auto text = PlatformFile::readAllText(projectFile);
        if (text.empty() && std::filesystem::file_size(projectFile) != 0)
            return rollback("Could not read project configuration");
        if (rewriteProjectConfig(text, source, destination, scope, projectRoot) && !write(projectFile, text))
            return rollback("Could not update project configuration");
    }
    registry.updateMovedAsset(source, destination, rewrittenAssets);
    return true;
}

bool AssetMaintenance::generateThumbnail(const AssetInfo& asset, const std::filesystem::path& output,
                                         const int requestedSize, std::string* error)
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
        if (texture.load(asset.path) && texture.getCompression() == TextureResource::Compression::None &&
            !texture.isHdr())
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const int sourceX = x * texture.getWidth() / size, sourceY = y * texture.getHeight() / size;
                    const std::size_t source = (static_cast<std::size_t>(sourceY) * texture.getWidth() + sourceX) * 4;
                    const std::size_t destination = (static_cast<std::size_t>(y) * size + x) * 4;
                    std::copy_n(texture.GetData().data() + source, 4, pixels.data() + destination);
                }
    }
    else if (asset.type == "Material")
    {
        MaterialResource material;
        if (material.load(asset.path))
        {
            const Vec4 color = material.getBaseColor();
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
        if (mesh.load(asset.path) && !mesh.GetVertices().empty())
        {
            std::vector<ThumbnailVertex> vertices;
            appendThumbnailMesh(mesh, Mat4::identity(), {0.28f, 0.59f, 0.9f, 1.0f}, vertices);
            renderThumbnailVertices(vertices, pixels, size);
        }
    }
    else if (asset.type == "Prefab")
    {
        World previewWorld;
        const auto roots = Prefab::Instantiate(previewWorld, asset.path);
        if (!roots.empty())
        {
            std::vector<ThumbnailVertex> vertices;
            for (const Entity entity : previewWorld.GetEntityManager().GetAliveEntities())
                if (const auto* meshComponent = previewWorld.GetComponent<MeshComponent>(entity))
                {
                    MeshResource mesh;
                    if (!mesh.load(meshComponent->meshPath))
                        continue;
                    Vec4 color{0.28f, 0.59f, 0.9f, 1.0f};
                    MaterialResource material;
                    if (!meshComponent->materialPath.empty() && material.load(meshComponent->materialPath))
                        color = material.getBaseColor();
                    appendThumbnailMesh(mesh, previewWorld.getWorldMatrix(entity), color, vertices);
                }
            renderThumbnailVertices(vertices, pixels, size);
        }
    }
    if (!savePpm(output, size, pixels))
    {
        if (error)
            *error = "Could not write thumbnail";
        return false;
    }
    return true;
}

// 分块清单记录每块尺寸和顺序，读取端可只加载覆盖请求区间的块。
bool AssetMaintenance::chunkAsset(const std::filesystem::path& source, const std::filesystem::path& manifest,
                                  const std::size_t requestedChunkBytes, std::string* error)
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
        if (!PlatformFile::writeAllBytes(manifest.parent_path() / name.str(), chunk))
        {
            if (error)
                *error = "Could not write chunk";
            return false;
        }
        description << "chunk " << std::quoted(name.str()) << ' ' << count << '\n';
    }
    return PlatformFile::writeAllText(manifest, description.str());
}

bool ChunkedAssetStream::Open(const std::filesystem::path& manifest)
{
    m_chunks.clear();
    m_size = 0;
    m_chunkSize = 0;
    std::istringstream input(PlatformFile::readAllText(manifest));
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

std::vector<std::uint8_t> ChunkedAssetStream::read(const std::uint64_t offset, const std::size_t requested) const
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

std::uint64_t ChunkedAssetStream::getSize() const
{
    return m_size;
}
std::size_t ChunkedAssetStream::getChunkSize() const
{
    return m_chunkSize;
}
} // namespace tinyengine
