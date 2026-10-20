// 文件：Programs/SceneTool/SceneTool.cpp
// 作用：提供场景验证与转换的命令行入口。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
std::vector<MiniEngine::Entity> SortedEntities(const MiniEngine::World &world)
{
    std::vector<MiniEngine::Entity> result(world.GetEntityManager().GetAliveEntities().begin(),
                                           world.GetEntityManager().GetAliveEntities().end());
    std::sort(result.begin(), result.end());
    return result;
}
} // 结束匿名命名空间

int main(int argc, char **argv)
{
    using namespace MiniEngine;
    if (argc < 3)
    {
        std::cout
            << "Usage:\n  tinyengine_scene_tool <scene> list\n  tinyengine_scene_tool <scene> add <name>\n  tinyengine_scene_tool <scene> "
               "remove <index>\n  tinyengine_scene_tool <scene> transform <index> <px> <py> <pz> <rx> <ry> <rz> <sx> <sy> <sz>\n";
        return 1;
    }
    World world;
    if (!SceneSerializer::Load(world, argv[1]))
    {
        std::cerr << "Cannot load scene: " << argv[1] << '\n';
        return 2;
    }
    const std::string command = argv[2];
    auto entities = SortedEntities(world);
    if (command == "list")
    {
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            const auto *name = world.GetComponent<NameComponent>(entities[index]);
            const auto *transform = world.GetComponent<TransformComponent>(entities[index]);
            std::cout << index << "\t" << (name ? name->name : "Entity");
            if (transform)
                std::cout << "\tpos(" << transform->position.x << ',' << transform->position.y << ',' << transform->position.z << ')';
            std::cout << '\n';
        }
        return 0;
    }
    if (command == "add" && argc == 4)
        world.CreateEntity(argv[3]);
    else if (command == "remove" && argc == 4)
    {
        const std::size_t index = static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
        if (index >= entities.size())
            return 3;
        world.DestroyEntity(entities[index]);
    }
    else if (command == "transform" && argc == 13)
    {
        const std::size_t index = static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
        if (index >= entities.size())
            return 3;
        auto *transform = world.GetComponent<TransformComponent>(entities[index]);
        float *values[] = {&transform->position.x, &transform->position.y, &transform->position.z,
                           &transform->rotation.x, &transform->rotation.y, &transform->rotation.z,
                           &transform->scale.x,    &transform->scale.y,    &transform->scale.z};
        for (int value = 0; value < 9; ++value)
            *values[value] = std::strtof(argv[value + 4], nullptr);
    }
    else
    {
        std::cerr << "Invalid command\n";
        return 1;
    }
    if (!SceneSerializer::Save(world, argv[1]))
        return 4;
    std::cout << "Scene updated: " << argv[1] << '\n';
    return 0;
}
