// 文件：include/MiniEngine/Resource/AssetCooker.hpp
// 作用：验证并烘焙资产，生成运行时清单和平台输出。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
struct CookResult
{
    bool success = false;
    std::size_t cookedFiles = 0;
    std::size_t skippedFiles = 0;
    std::size_t removedFiles = 0;
    bool cancelled = false;
    std::vector<std::string> errors;
};

struct CookOptions
{
    bool incremental = true;
    std::atomic<bool> *cancel = nullptr;
    std::function<void(float, const std::string &)> progress;
    std::function<bool(const std::filesystem::path &)> include;
    std::function<bool(const std::filesystem::path &, const std::filesystem::path &, std::string &)> transform;
};

class AssetCooker
{
  public:
    /// @brief 将源资产烘焙为目标平台格式。
    /// @param assetDirectory 相关文件、资源或目录路径。
    /// @param cookedDirectory 相关文件、资源或目录路径。
    /// @param outputPak 接口所需的输入参数。
    /// @param options 控制本次操作的选项。
    /// @return 返回接口计算或查询得到的结果。
    static CookResult Cook(const std::filesystem::path &assetDirectory, const std::filesystem::path &cookedDirectory,
                           const std::filesystem::path &outputPak, const CookOptions &options = {});
};
} // namespace MiniEngine
