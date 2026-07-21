// 文件：include/MiniEngine/Platform/Platform.hpp
// 作用：封装计时、文件、动态库、进程和平台输入等操作系统差异。
// 约定：平台差异集中封装在此层，上层模块不直接调用系统 API。
// 类型：公开声明。

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
class PlatformFile
{
  public:
    /// @brief 判断指定文件或目录是否存在。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Exists(const std::filesystem::path &path);
    /// @brief 读取 `AllBytes`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path &path);
    /// @brief 读取 `AllText`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回对应的文本或路径。
    static std::string ReadAllText(const std::filesystem::path &path);
    /// @brief 写入 `AllBytes`。
    /// @param path 文件、资源或目录路径。
    /// @param data 要读取、写入或发送的数据。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool WriteAllBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &data);
    /// @brief 写入 `AllText`。
    /// @param path 文件、资源或目录路径。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool WriteAllText(const std::filesystem::path &path, const std::string &text);
    /// @brief 将输入值规范化。
    /// @param path 文件、资源或目录路径。
    /// @return 返回对应的文本或路径。
    static std::filesystem::path Normalize(const std::filesystem::path &path);
    /// @brief 挂载 `Directory`。
    /// @param directory 相关文件、资源或目录路径。
    /// @param priority 任务或资源请求优先级。
    static void MountDirectory(const std::filesystem::path &directory, int priority = 0);
    /// @brief 挂载 `Pak`。
    /// @param archive 目标归档路径或归档对象。
    /// @param priority 任务或资源请求优先级。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool MountPak(const std::filesystem::path &archive, int priority = 0);
    /// @brief 清空 `Mounts`。
    static void ClearMounts();
};

class EngineClock
{
  public:
    /// @brief 将对象恢复为初始状态。
    void Reset();
    /// @brief 推进一次按帧更新。
    /// @return 返回计算得到的数值、数量或索引。
    float Tick();
    /// @brief 获取 `TotalSeconds`。
    /// @return 返回计算得到的数值、数量或索引。
    double GetTotalSeconds() const;

  private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_start = Clock::now();
    Clock::time_point m_previous = m_start;
};

class FileWatcher
{
  public:
    /// @brief 判断是否包含 `Changed` 条件。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool HasChanged(const std::filesystem::path &path);

  private:
    std::filesystem::file_time_type m_lastWrite{};
    bool m_initialized = false;
};
} // namespace MiniEngine
