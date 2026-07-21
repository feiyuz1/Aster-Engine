// 文件：include/MiniEngine/Project/Project.hpp
// 作用：定义项目设置、场景切换、Prefab 实例化以及编辑/运行态快照。
// 约定：编辑态和运行态场景相互隔离，运行结束后恢复进入前快照。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Core/Config.hpp"
#include "MiniEngine/Types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
class World;

// 项目文件的稳定配置模型；路径按项目根目录解析，不依赖启动进程的当前目录。
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

    bool Load(const std::filesystem::path &path);
    bool Save(const std::filesystem::path &path) const;
    std::filesystem::path Resolve(const std::filesystem::path &path) const;
};

// 场景事务边界：加载先在临时数据中验证，成功后再替换活动 World。
class SceneManager
{
  public:
    explicit SceneManager(World *world = nullptr);
    void SetWorld(World *world);
    void NewScene();
    bool LoadScene(const std::filesystem::path &path);
    bool SaveScene(const std::filesystem::path &path = {});
    std::string CreateSnapshot() const;
    bool RestoreSnapshot(const std::string &snapshot);
    const std::filesystem::path &GetCurrentScene() const;

  private:
    World *m_world = nullptr;
    std::filesystem::path m_currentScene;
};

// 项目可替换的运行时入口，生命周期夹在 Engine 初始化完成与子系统关闭之间。
class GameInstance
{
  public:
    virtual ~GameInstance() = default;
    virtual void OnInitialize()
    {
    }
    virtual void OnSceneLoaded(const std::filesystem::path &)
    {
    }
    virtual void OnUpdate(float)
    {
    }
    virtual void OnShutdown()
    {
    }
};

// Prefab 实例化生成新的实体集合，资产中的实体编号仅用于重建内部引用，不能泄漏到 World。
class Prefab
{
  public:
    static bool Save(const World &world, Entity root, const std::filesystem::path &path);
    static std::vector<Entity> Instantiate(World &world, const std::filesystem::path &path, Entity parent = InvalidEntity);
};

// SaveGame 保存面向玩家进度的数据，不等价于编辑器场景序列化格式。
class SaveGame
{
  public:
    void Set(std::string key, std::string value);
    std::string GetString(const std::string &key, const std::string &fallback = {}) const;
    int GetInt(const std::string &key, int fallback = 0) const;
    float GetFloat(const std::string &key, float fallback = 0.0f) const;
    bool GetBool(const std::string &key, bool fallback = false) const;
    bool Save(const std::filesystem::path &path) const;
    bool Load(const std::filesystem::path &path);

  private:
    Config m_data;
};
} // namespace MiniEngine
