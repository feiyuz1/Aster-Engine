// 文件：include/MiniEngine/ECS/SceneSerializer.hpp
// 作用：在 World 与场景文本格式之间进行序列化和反序列化。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
class World;
class SceneSerializer
{
  public:
    /// @brief 将对象序列化为持久化数据。
    /// @param world 要读取或修改的 World。
    /// @return 返回对应的文本或路径。
    static std::string Serialize(const World &world);
    /// @brief 从持久化数据恢复对象。
    /// @param world 要读取或修改的 World。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Deserialize(World &world, const std::string &text);
    /// @brief 保存当前数据。
    /// @param world 要读取或修改的 World。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Save(const World &world, const std::filesystem::path &path);
    /// @brief 加载数据或资源。
    /// @param world 要读取或修改的 World。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Load(World &world, const std::filesystem::path &path);
};
} // 结束 MiniEngine 命名空间
