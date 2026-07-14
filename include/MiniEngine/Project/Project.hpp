#pragma once

#include "MiniEngine/Types.hpp"
#include "MiniEngine/Core/Config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    class World;

    struct ProjectSettings
    {
        std::string name = "TinyGame";
        std::filesystem::path projectFile;
        std::filesystem::path assetDirectory = "assets";
        std::filesystem::path startupScene = "Saved/Demo.scene";
        std::filesystem::path cookedDirectory = "Saved/Cooked";
        std::filesystem::path packageArchive;
        std::filesystem::path inputBindings = "assets/default.input";
        std::filesystem::path environmentTexture;
        float fixedStep = 1.0f / 60.0f;

        bool Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;
        std::filesystem::path Resolve(const std::filesystem::path& path) const;
    };

    class SceneManager
    {
    public:
        explicit SceneManager(World* world = nullptr);
        void SetWorld(World* world);
        void NewScene();
        bool LoadScene(const std::filesystem::path& path);
        bool SaveScene(const std::filesystem::path& path = {});
        std::string CreateSnapshot() const;
        bool RestoreSnapshot(const std::string& snapshot);
        const std::filesystem::path& GetCurrentScene() const;
    private:
        World* m_world = nullptr;
        std::filesystem::path m_currentScene;
    };

    class GameInstance
    {
    public:
        virtual ~GameInstance() = default;
        virtual void OnInitialize() {}
        virtual void OnSceneLoaded(const std::filesystem::path&) {}
        virtual void OnUpdate(float) {}
        virtual void OnShutdown() {}
    };

    class Prefab
    {
    public:
        static bool Save(const World& world, Entity root, const std::filesystem::path& path);
        static std::vector<Entity> Instantiate(World& world, const std::filesystem::path& path, Entity parent = InvalidEntity);
    };

    class SaveGame
    {
    public:
        void Set(std::string key, std::string value);
        std::string GetString(const std::string& key, const std::string& fallback = {}) const;
        int GetInt(const std::string& key, int fallback = 0) const;
        float GetFloat(const std::string& key, float fallback = 0.0f) const;
        bool GetBool(const std::string& key, bool fallback = false) const;
        bool Save(const std::filesystem::path& path) const;
        bool Load(const std::filesystem::path& path);
    private:
        Config m_data;
    };
}
