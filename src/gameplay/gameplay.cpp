// 文件：src/gameplay/gameplay.cpp
// 作用：组织 GameInstance、Pawn、Controller 等运行时游戏框架对象。
// 约定：该层只编排运行时对象，不直接承担平台或渲染实现。
// 类型：实现文件。

#include "tinyengine/gameplay/gameplay.h"

#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/input/input_system.h"
#include "tinyengine/physics/physics_world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tinyengine
{
TimerHandle TimerManager::setTimer(const float delaySeconds, std::function<void()> callback, const bool looping)
{
    if (!callback || delaySeconds < 0.0f || (looping && delaySeconds <= 0.0f))
        return {};
    const TimerHandle handle{m_nextId++};
    m_timers.emplace(handle.id, Timer{delaySeconds, delaySeconds, looping, false, std::move(callback)});
    return handle;
}

bool TimerManager::clearTimer(const TimerHandle handle)
{
    return handle.isValid() && m_timers.erase(handle.id) != 0;
}

bool TimerManager::pauseTimer(const TimerHandle handle, const bool paused)
{
    const auto found = m_timers.find(handle.id);
    if (found == m_timers.end())
        return false;
    found->second.paused = paused;
    return true;
}

bool TimerManager::isActive(const TimerHandle handle) const
{
    return handle.isValid() && m_timers.count(handle.id) != 0;
}

void TimerManager::Update(const float deltaTime)
{
    if (deltaTime < 0.0f)
        return;
    std::vector<std::uint64_t> due;
    for (auto& pair : m_timers)
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

void TimerManager::clear()
{
    m_timers.clear();
}

void GameplayFramework::initialize(World* world, InputSystem* input, PhysicsWorld* physics)
{
    m_world = world;
    m_input = input;
    m_physics = physics;
}

void GameplayFramework::shutdown()
{
    stopPlay();
    m_gameMode.reset();
    m_world = nullptr;
    m_input = nullptr;
    m_physics = nullptr;
}

void GameplayFramework::startPlay()
{
    if (m_playing || !m_world)
        return;
    m_playing = true;
    if (m_gameMode)
        m_gameMode->onStart(*m_world);
}

void GameplayFramework::stopPlay()
{
    if (!m_playing)
        return;
    if (m_gameMode && m_world)
        m_gameMode->onStop(*m_world);
    m_timers.clear();
    m_playing = false;
}

void GameplayFramework::Update(const float deltaTime)
{
    if (!m_playing || !m_world || deltaTime <= 0.0f)
        return;
    m_timers.Update(deltaTime);
    if (m_gameMode)
        m_gameMode->onUpdate(*m_world, deltaTime);
    m_world->GetComponentManager().ForEach<PawnComponent, CharacterControllerComponent>(
        [&](const Entity entity, PawnComponent& pawn, CharacterControllerComponent& controller)
        {
            if (!pawn.playerControlled || pawn.controllerIndex != 0 || !m_input)
                return;
            if (const auto* active = m_world->GetComponent<ActiveComponent>(entity); active && !active->active)
                return;
            Vec3 desired{m_input->getAxis("MoveX"), 0.0f, m_input->getAxis("MoveZ")};
            const float magnitude = length(desired);
            if (magnitude > 1.0f)
                desired = desired / magnitude;
            const float control = controller.grounded ? 1.0f : std::clamp(controller.airControl, 0.0f, 1.0f);
            const float speed = m_input->isDown("Sprint") ? controller.sprintSpeed : controller.movementSpeed;
            controller.velocity.x = desired.x * speed * control;
            controller.velocity.z = desired.z * speed * control;
            if (controller.grounded && m_input->wasPressed("Jump"))
            {
                controller.velocity.y = controller.jumpSpeed;
                controller.grounded = false;
            }
            controller.velocity.y -= controller.gravity * deltaTime;
            const Vec3 displacement = controller.velocity * deltaTime;
            Vec3 applied = displacement;
            if (m_physics && m_world->HasComponent<ColliderComponent>(entity))
            {
                const auto move = m_physics->moveCharacterController(*m_world, entity, displacement);
                applied = move.appliedDisplacement;
                controller.grounded = move.grounded;
                if (move.hitCeiling)
                    controller.velocity.y = 0.0f;
            }
            else if (auto* transform = m_world->GetComponent<TransformComponent>(entity))
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

void GameplayFramework::setGameMode(std::unique_ptr<GameMode> gameMode)
{
    if (m_playing && m_gameMode && m_world)
        m_gameMode->onStop(*m_world);
    m_gameMode = std::move(gameMode);
    if (m_playing && m_gameMode && m_world)
        m_gameMode->onStart(*m_world);
}

TimerManager& GameplayFramework::GetTimerManager()
{
    return m_timers;
}

bool GameplayFramework::isPlaying() const
{
    return m_playing;
}
} // namespace tinyengine
