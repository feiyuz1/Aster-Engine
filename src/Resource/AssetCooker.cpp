// 文件：src/Resource/AssetCooker.cpp
// 作用：验证并烘焙资产，生成运行时清单和平台输出。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/AssetCooker.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace MiniEngine
{
namespace
{
struct Entry
{
    std::int64_t stamp = 0;
    std::uintmax_t size = 0;
};
using Manifest = std::unordered_map<std::string, Entry>;

Manifest LoadManifest(const std::filesystem::path &path)
{
    Manifest result;
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string signature;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINYCOOK" || version != 1)
        return result;
    std::string token, relative;
    Entry entry;
    while (input >> token)
        if (token == "file" && (input >> std::quoted(relative) >> entry.stamp >> entry.size))
            result[relative] = entry;
        else
            break;
    return result;
}

bool SaveManifest(const std::filesystem::path &path, const Manifest &manifest)
{
    std::vector<std::string> paths;
    for (const auto &pair : manifest)
        paths.push_back(pair.first);
    std::sort(paths.begin(), paths.end());
    std::ostringstream output;
    output << "TINYCOOK 1\n";
    for (const auto &relative : paths)
    {
        const auto &entry = manifest.at(relative);
        output << "file " << std::quoted(relative) << ' ' << entry.stamp << ' ' << entry.size << '\n';
    }
    return PlatformFile::WriteAllText(path, output.str());
}

bool Cancelled(const CookOptions &options)
{
    return options.cancel && options.cancel->load();
}
} // 匿名命名空间

CookResult AssetCooker::Cook(const std::filesystem::path &assetDirectory, const std::filesystem::path &cookedDirectory,
                             const std::filesystem::path &outputPak, const CookOptions &options)
{
    CookResult result;
    if (!std::filesystem::is_directory(assetDirectory))
    {
        result.errors.push_back("Asset directory does not exist");
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(cookedDirectory, error);
    if (error)
    {
        result.errors.push_back(error.message());
        return result;
    }
    const auto manifestPath = cookedDirectory / ".cookmanifest";
    const Manifest previous = options.incremental ? LoadManifest(manifestPath) : Manifest{};
    Manifest current;
    std::vector<std::filesystem::path> sources;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(assetDirectory))
        if (entry.is_regular_file() && entry.path().extension() != ".meta" && (!options.include || options.include(entry.path())))
            sources.push_back(entry.path());
    std::sort(sources.begin(), sources.end());
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        if (Cancelled(options))
        {
            result.cancelled = true;
            result.errors.push_back("Cook cancelled");
            return result;
        }
        const auto relative = std::filesystem::relative(sources[index], assetDirectory).lexically_normal();
        const std::string key = relative.generic_string();
        const Entry entry{static_cast<std::int64_t>(std::filesystem::last_write_time(sources[index]).time_since_epoch().count()),
                          std::filesystem::file_size(sources[index])};
        current[key] = entry;
        const auto old = previous.find(key);
        const auto destination = cookedDirectory / relative;
        if (options.incremental && old != previous.end() && old->second.stamp == entry.stamp && old->second.size == entry.size &&
            std::filesystem::is_regular_file(destination))
            ++result.skippedFiles;
        else
        {
            std::filesystem::create_directories(destination.parent_path(), error);
            std::string transformError;
            const bool transformed = !error && options.transform ? options.transform(sources[index], destination, transformError) : false;
            if (!error && !options.transform)
                std::filesystem::copy_file(sources[index], destination, std::filesystem::copy_options::overwrite_existing, error);
            if ((options.transform && !transformed) || error)
            {
                result.errors.push_back(key + ": " + (transformError.empty() ? error.message() : transformError));
                error.clear();
                // 即使源文件时间戳未变，重试也不能接受上次遗留的不完整文件。
                std::filesystem::remove(destination, error);
                if (error)
                {
                    result.errors.push_back(key + ": Failed to remove incomplete output: " + error.message());
                    // 输出文件被占用时可能清理失败；下次尝试前必须使缓存失效。
                    std::filesystem::remove(manifestPath, error);
                    if (error)
                        result.errors.push_back("Failed to invalidate cook manifest: " + error.message());
                }
                error.clear();
            }
            else
                ++result.cookedFiles;
        }
        if (options.progress)
            options.progress(sources.empty() ? 0.8f : 0.8f * static_cast<float>(index + 1) / static_cast<float>(sources.size()), key);
    }
    // 失败时保留上次成功的清单，避免重试误收过期输出。
    if (!result.errors.empty())
        return result;
    for (const auto &old : previous)
    {
        if (current.count(old.first))
            continue;
        const auto relative = std::filesystem::path(old.first).lexically_normal();
        if (relative.is_absolute() || old.first.find("..") != std::string::npos)
            continue;
        const auto stale = cookedDirectory / relative;
        if (std::filesystem::is_regular_file(stale) && std::filesystem::remove(stale, error))
            ++result.removedFiles;
        error.clear();
    }
    if (!SaveManifest(manifestPath, current))
        result.errors.push_back("Failed to write cook manifest");
    if (Cancelled(options))
    {
        result.cancelled = true;
        result.errors.push_back("Cook cancelled");
        return result;
    }
    if (options.progress)
        options.progress(0.9f, "Packing archive");
    result.success = result.errors.empty() && PakArchive::PackDirectory(cookedDirectory, outputPak);
    if (!result.success && result.errors.empty())
        result.errors.push_back("Failed to create archive");
    if (options.progress)
        options.progress(1.0f, result.success ? "Complete" : "Failed");
    return result;
}
} // 结束 MiniEngine 命名空间
