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
    static std::string Serialize(const World &world);
    static bool Deserialize(World &world, const std::string &text);
    static bool Save(const World &world, const std::filesystem::path &path);
    static bool Load(World &world, const std::filesystem::path &path);
};
} // namespace MiniEngine
