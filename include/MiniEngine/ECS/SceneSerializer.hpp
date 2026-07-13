#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
    class World;
    class SceneSerializer
    {
    public:
        static std::string Serialize(const World& world);
        static bool Deserialize(World& world, const std::string& text);
        static bool Save(const World& world, const std::filesystem::path& path);
        static bool Load(World& world, const std::filesystem::path& path);
    };
}
