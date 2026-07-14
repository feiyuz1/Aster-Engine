#pragma once

#include "MiniEngine/Types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace MiniEngine
{
class InputSystem;
class PhysicsWorld;
class World;

struct TimerHandle
{
    std::uint64_t id = 0;
    bool IsValid() const
    {
        return id != 0;
    }
};

class TimerManager
{
  public:
    TimerHandle SetTimer(float delaySeconds, std::function<void()> callback, bool looping = false);
    bool ClearTimer(TimerHandle handle);
    bool PauseTimer(TimerHandle handle, bool paused = true);
    bool IsActive(TimerHandle handle) const;
    void Update(float deltaTime);
    void Clear();

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
    virtual ~GameMode() = default;
    virtual void OnStart(World &)
    {
    }
    virtual void OnUpdate(World &, float)
    {
    }
    virtual void OnStop(World &)
    {
    }
};

class GameplayFramework
{
  public:
    void Initialize(World *world, InputSystem *input, PhysicsWorld *physics);
    void Shutdown();
    void StartPlay();
    void StopPlay();
    void Update(float deltaTime);
    void SetGameMode(std::unique_ptr<GameMode> gameMode);
    TimerManager &GetTimerManager();
    bool IsPlaying() const;

  private:
    World *m_world = nullptr;
    InputSystem *m_input = nullptr;
    PhysicsWorld *m_physics = nullptr;
    std::unique_ptr<GameMode> m_gameMode;
    TimerManager m_timers;
    bool m_playing = false;
};
} // namespace MiniEngine
