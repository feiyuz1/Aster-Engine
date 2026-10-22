// 文件：include/tinyengine/gameplay/gameplay.h
// 作用：组织 GameInstance、Pawn、Controller 等运行时游戏框架对象。
// 约定：该层只编排运行时对象，不直接承担平台或渲染实现。
// 类型：公开声明。

#pragma once

#include "tinyengine/types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace tinyengine
{
class InputSystem;
class PhysicsWorld;
class World;

struct TimerHandle
{
    std::uint64_t id = 0;
    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isValid() const
    {
        return id != 0;
    }
};

class TimerManager
{
  public:
    /// @brief 设置 `Timer`。
    /// @param delaySeconds 时间值，单位按接口约定为秒。
    /// @param callback 满足条件时调用的回调函数。
    /// @param looping 是否循环播放。
    /// @return 返回接口计算或查询得到的结果。
    TimerHandle setTimer(float delaySeconds, std::function<void()> callback, bool looping = false);
    /// @brief 清空 `Timer`。
    /// @param handle 目标资源或对象句柄。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool clearTimer(TimerHandle handle);
    /// @brief 暂停 `Timer`。
    /// @param handle 目标资源或对象句柄。
    /// @param paused 是否启用对应行为。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool pauseTimer(TimerHandle handle, bool paused = true);
    /// @brief 判断是否满足 `Active` 条件。
    /// @param handle 目标资源或对象句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isActive(TimerHandle handle) const;
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 清空对象保存的状态和数据。
    void clear();

  private:
    struct Timer
    {
        float remaining = 0.0f;
        float interval = 0.0f;
        bool looping = false;
        bool paused = false;
        std::function<void()> callback;
    };
    std::unordered_map<std::uint64_t, Timer> m_timers;
    std::uint64_t m_nextId = 1;
};

class GameMode
{
  public:
    /// @brief 析构 `GameMode` 对象并释放其持有资源。
    virtual ~GameMode() = default;
    /// @brief 响应 `Start`。
    virtual void onStart(World&) {}
    /// @brief 响应 `Update`。
    virtual void onUpdate(World&, float) {}
    /// @brief 响应 `Stop`。
    virtual void onStop(World&) {}
};

class GameplayFramework
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param world 要读取或修改的 World。
    /// @param input 接口所需的输入参数。
    /// @param physics 接口所需的输入参数。
    void initialize(World* world, InputSystem* input, PhysicsWorld* physics);
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown();
    /// @brief 启动 `Play`。
    void startPlay();
    /// @brief 停止 `Play`。
    void stopPlay();
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 设置 `GameMode`。
    /// @param gameMode 目标状态、模式或类型。
    void setGameMode(std::unique_ptr<GameMode> gameMode);
    /// @brief 获取 `TimerManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    TimerManager& GetTimerManager();
    /// @brief 判断是否满足 `Playing` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isPlaying() const;

  private:
    World* m_world = nullptr;
    InputSystem* m_input = nullptr;
    PhysicsWorld* m_physics = nullptr;
    std::unique_ptr<GameMode> m_gameMode;
    TimerManager m_timers;
    bool m_playing = false;
};
} // namespace tinyengine
