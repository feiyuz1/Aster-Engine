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
    std::string projectId;
    std::string codeModule;
    bool codeRequired = false;
    std::filesystem::path codeManifest;
    std::filesystem::path projectFile;
    std::filesystem::path assetDirectory = "assets";
    std::filesystem::path startupScene = "Saved/Demo.scene";
    std::filesystem::path cookedDirectory = "Saved/Cooked";
    std::filesystem::path packageArchive;
    std::filesystem::path inputBindings = "assets/default.input";
    std::filesystem::path environmentTexture;
    float fixedStep = 1.0f / 60.0f;

    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;
    /// @brief 解析依赖或查找最终结果。
    /// @param path 文件、资源或目录路径。
    /// @return 返回对应的文本或路径。
    std::filesystem::path Resolve(const std::filesystem::path &path) const;
};

// 场景事务边界：加载先在临时数据中验证，成功后再替换活动 World。
class SceneManager
{
  public:
    /// @brief 构造 `SceneManager` 对象。
    /// @param world 要读取或修改的 World。
    explicit SceneManager(World *world = nullptr);
    /// @brief 设置 `World`。
    /// @param world 要读取或修改的 World。
    void SetWorld(World *world);
    /// @brief 新建 `Scene`。
    void NewScene();
    /// @brief 加载 `Scene`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool LoadScene(const std::filesystem::path &path);
    /// @brief 保存 `Scene`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveScene(const std::filesystem::path &path = {});
    /// @brief 创建 `Snapshot`。
    /// @return 返回对应的文本或路径。
    std::string CreateSnapshot() const;
    /// @brief 恢复 `Snapshot`。
    /// @param snapshot 要保存、发送或应用的状态快照。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RestoreSnapshot(const std::string &snapshot);
    /// @brief 获取 `CurrentScene`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::filesystem::path &GetCurrentScene() const;

  private:
    World *m_world = nullptr;
    std::filesystem::path m_currentScene;
};

// 项目可替换的运行时入口，生命周期夹在 Engine 初始化完成与子系统关闭之间。
class GameInstance
{
  public:
    /// @brief 析构 `GameInstance` 对象并释放其持有资源。
    virtual ~GameInstance() = default;
    /// @brief 响应 `Initialize`。
    virtual void OnInitialize()
    {
    }
    /// @brief 响应 `SceneLoaded`。
    virtual void OnSceneLoaded(const std::filesystem::path &)
    {
    }
    /// @brief 响应 `Update`。
    virtual void OnUpdate(float)
    {
    }
    /// @brief 响应 `Shutdown`。
    virtual void OnShutdown()
    {
    }
};

// Prefab 实例化生成新的实体集合，资产中的实体编号仅用于重建内部引用，不能泄漏到 World。
class Prefab
{
  public:
    /// @brief 保存当前数据。
    /// @param world 要读取或修改的 World。
    /// @param root 根目录、根节点或根实体。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Save(const World &world, Entity root, const std::filesystem::path &path);
    /// @brief 实例化资产并重建内部实体引用。
    /// @param world 要读取或修改的 World。
    /// @param path 文件、资源或目录路径。
    /// @param parent 相关实体句柄。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    static std::vector<Entity> Instantiate(World &world, const std::filesystem::path &path, Entity parent = InvalidEntity);
};

// SaveGame 保存面向玩家进度的数据，不等价于编辑器场景序列化格式。
class SaveGame
{
  public:
    /// @brief 设置目标对象或值。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    void Set(std::string key, std::string value);
    /// @brief 获取 `String`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回对应的文本或路径。
    std::string GetString(const std::string &key, const std::string &fallback = {}) const;
    /// @brief 获取 `Int`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回计算得到的数值、数量或索引。
    int GetInt(const std::string &key, int fallback = 0) const;
    /// @brief 获取 `Float`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回计算得到的数值、数量或索引。
    float GetFloat(const std::string &key, float fallback = 0.0f) const;
    /// @brief 获取 `Bool`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool GetBool(const std::string &key, bool fallback = false) const;
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);

  private:
    Config m_data;
};
} // 结束 MiniEngine 命名空间
