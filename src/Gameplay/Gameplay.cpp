// 文件：src/Gameplay/Gameplay.cpp
// 作用：组织 GameInstance、Pawn、Controller 等运行时游戏框架对象。
// 约定：该层只编排运行时对象，不直接承担平台或渲染实现。
// 类型：实现文件。

#include "MiniEngine/Gameplay/Gameplay.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace MiniEngine
{
TimerHandle TimerManager::SetTimer(const float delaySeconds, std::function<void()> callback, const bool looping)
{
    if (!callback || delaySeconds < 0.0f || (looping && delaySeconds <= 0.0f))
        return {};
    const TimerHandle handle{m_nextId++};
    m_timers.emplace(handle.id, Timer{delaySeconds, delaySeconds, looping, false, std::move(callback)});
    return handle;
}

bool TimerManager::ClearTimer(const TimerHandle handle)
{
    return handle.IsValid() && m_timers.erase(handle.id) != 0;
}

bool TimerManager::PauseTimer(const TimerHandle handle, const bool paused)
{
    const auto found = m_timers.find(handle.id);
    if (found == m_timers.end())
        return false;
    found->second.paused = paused;
    return true;
}

bool TimerManager::IsActive(const TimerHandle handle) const
{
    return handle.IsValid() && m_timers.count(handle.id) != 0;
}

void TimerManager::Update(const float deltaTime)
{
    if (deltaTime < 0.0f)
        return;
    std::vector<std::uint64_t> due;
    for (auto &pair : m_timers)
    {
        if (pair.second.paused)
            continue;
        pair.second.remaining -= deltaTime;
        if (pair.second.remaining <= 0.0f)
            due.push_back(pair.first);
    }
    for (const auto id : due)
    {
        const auto found = m_timers.find(id);
        if (found == m_timers.end())
            continue;
        auto callback = found->second.callback;
        if (found->second.looping)
        {
            const float overshoot = -found->second.remaining;
            found->second.remaining = found->second.interval - std::fmod(overshoot, found->second.interval);
        }
        else
            m_timers.erase(found);
        if (callback)
            callback();
    }
}

void TimerManager::Clear()
{
    m_timers.clear();
}

void GameplayFramework::Initialize(World *world, InputSystem *input, PhysicsWorld *physics)
{
    m_world = world;
    m_input = input;
    m_physics = physics;
}

void GameplayFramework::Shutdown()
{
    StopPlay();
    m_gameMode.reset();
    m_world = nullptr;
    m_input = nullptr;
    m_physics = nullptr;
}

void GameplayFramework::StartPlay()
{
    if (m_playing || !m_world)
        return;
    m_playing = true;
    if (m_gameMode)
        m_gameMode->OnStart(*m_world);
}

void GameplayFramework::StopPlay()
{
    if (!m_playing)
        return;
    if (m_gameMode && m_world)
        m_gameMode->OnStop(*m_world);
    m_timers.Clear();
    m_playing = false;
}

void GameplayFramework::Update(const float deltaTime)
{
    if (!m_playing || !m_world || deltaTime <= 0.0f)
        return;
    m_timers.Update(deltaTime);
    if (m_gameMode)
        m_gameMode->OnUpdate(*m_world, deltaTime);
    m_world->GetComponentManager().ForEach<PawnComponent, CharacterControllerComponent>(
        [&](const Entity entity, PawnComponent &pawn, CharacterControllerComponent &controller) {
            if (!pawn.playerControlled || pawn.controllerIndex != 0 || !m_input)
                return;
            if (const auto *active = m_world->GetComponent<ActiveComponent>(entity); active && !active->active)
                return;
            Vec3 desired{m_input->GetAxis("MoveX"), 0.0f, m_input->GetAxis("MoveZ")};
            const float length = Length(desired);
            if (length > 1.0f)
                desired = desired / length;
            const float control = controller.grounded ? 1.0f : std::clamp(controller.airControl, 0.0f, 1.0f);
            const float speed = m_input->IsDown("Sprint") ? controller.sprintSpeed : controller.movementSpeed;
            controller.velocity.x = desired.x * speed * control;
            controller.velocity.z = desired.z * speed * control;
            if (controller.grounded && m_input->WasPressed("Jump"))
            {
                controller.velocity.y = controller.jumpSpeed;
                controller.grounded = false;
            }
            controller.velocity.y -= controller.gravity * deltaTime;
            const Vec3 displacement = controller.velocity * deltaTime;
            Vec3 applied = displacement;
            if (m_physics && m_world->HasComponent<ColliderComponent>(entity))
            {
                const auto move = m_physics->MoveCharacterController(*m_world, entity, displacement);
                applied = move.appliedDisplacement;
                controller.grounded = move.grounded;
                if (move.hitCeiling)
                    controller.velocity.y = 0.0f;
            }
            else if (auto *transform = m_world->GetComponent<TransformComponent>(entity))
                transform->position += displacement;
            if (displacement.y < 0.0f && std::abs(applied.y - displacement.y) > 0.0001f)
            {
                controller.grounded = true;
                controller.velocity.y = 0.0f;
            }
            else if (displacement.y > 0.0f && std::abs(applied.y - displacement.y) > 0.0001f)
                controller.velocity.y = 0.0f;
        });
}

void GameplayFramework::SetGameMode(std::unique_ptr<GameMode> gameMode)
{
    if (m_playing && m_gameMode && m_world)
        m_gameMode->OnStop(*m_world);
    m_gameMode = std::move(gameMode);
    if (m_playing && m_gameMode && m_world)
        m_gameMode->OnStart(*m_world);
}

TimerManager &GameplayFramework::GetTimerManager()
{
    return m_timers;
}

bool GameplayFramework::IsPlaying() const
{
    return m_playing;
}
} // 结束 MiniEngine 命名空间
