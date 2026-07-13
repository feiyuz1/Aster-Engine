#pragma once

#include <filesystem>

namespace MiniEngine
{
    class World;
    class SceneSerializer
    {
    public:
        static bool Save(const World& world, const std::filesystem::path& path);
        static bool Load(World& world, const std::filesystem::path& path);
    };
}
