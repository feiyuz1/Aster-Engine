#include "MiniEngine/Render/Renderer.hpp"

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MiniEngine
{
    bool NullRenderDevice::Initialize()
    {
        return true;
    }

    void NullRenderDevice::Shutdown()
    {
    }

    bool NullRenderDevice::PumpEvents()
    {
        return true;
    }

    const InputState& NullRenderDevice::GetInputState() const
    {
        return m_inputState;
    }

    void NullRenderDevice::BeginFrame()
    {
    }

    void NullRenderDevice::Draw(const RenderItem& item)
    {
        (void)item;
    }

    void NullRenderDevice::EndFrame()
    {
    }

    void NullRenderDevice::Present()
    {
    }

    void RenderQueue::AddItem(const RenderItem& item)
    {
        m_items.push_back(item);
    }

    void RenderQueue::Sort()
    {
        std::sort(m_items.begin(), m_items.end(), [](const RenderItem& lhs, const RenderItem& rhs)
        {
            return lhs.distanceToCamera < rhs.distanceToCamera;
        });
    }

    void RenderQueue::Clear()
    {
        m_items.clear();
    }

    const std::vector<RenderItem>& RenderQueue::GetItems() const
    {
        return m_items;
    }

    void ShaderManager::Initialize()
    {
    }

    void ShaderManager::Shutdown()
    {
    }

    void MaterialSystem::Initialize()
    {
    }

    void MaterialSystem::Shutdown()
    {
    }

    void Renderer::SetLogger(Logger* logger)
    {
        m_logger = logger;
    }

    bool Renderer::Initialize(std::unique_ptr<RenderDevice> device)
    {
        m_device = std::move(device);
        if (m_device == nullptr || !m_device->Initialize())
        {
            return false;
        }

        m_shaderManager.Initialize();
        m_materialSystem.Initialize();
        return true;
    }

    void Renderer::Shutdown()
    {
        m_renderQueue.Clear();
        m_materialSystem.Shutdown();
        m_shaderManager.Shutdown();

        if (m_device != nullptr)
        {
            m_device->Shutdown();
            m_device.reset();
        }
    }

    bool Renderer::PumpEvents()
    {
        if (m_device == nullptr)
        {
            return false;
        }
        return m_device->PumpEvents();
    }

    const InputState& Renderer::GetInputState() const
    {
        static const InputState emptyState{};
        if (m_device == nullptr)
        {
            return emptyState;
        }
        return m_device->GetInputState();
    }

    void Renderer::BuildRenderQueue(World& world)
    {
        m_renderQueue.Clear();

        auto& entities = world.GetEntityManager();
        auto& components = world.GetComponentManager();

        for (const Entity entity : entities.GetAliveEntities())
        {
            const auto* mesh = components.Get<MeshComponent>(entity);
            if (mesh == nullptr)
            {
                continue;
            }

            float distance = 0.0f;
            const auto* transform = components.Get<TransformComponent>(entity);
            if (transform != nullptr)
            {
                distance = std::sqrt(
                    transform->position.x * transform->position.x +
                    transform->position.y * transform->position.y +
                    transform->position.z * transform->position.z);
            }

            m_renderQueue.AddItem(RenderItem{
                entity,
                distance,
                transform != nullptr ? transform->position.x : 0.0f,
                transform != nullptr ? transform->position.y : 0.0f,
                transform != nullptr ? transform->position.z : 0.0f,
                transform != nullptr ? transform->rotation.x : 0.0f,
                transform != nullptr ? transform->rotation.y : 0.0f,
                transform != nullptr ? transform->rotation.z : 0.0f,
                transform != nullptr ? transform->scale.x : 1.0f,
                transform != nullptr ? transform->scale.y : 1.0f,
                transform != nullptr ? transform->scale.z : 1.0f,
                mesh->meshPath
            });
        }

        m_renderQueue.Sort();
    }

    void Renderer::RenderFrame(World& world)
    {
        if (m_device == nullptr)
        {
            return;
        }

        BuildRenderQueue(world);

        m_device->BeginFrame();
        for (const auto& item : m_renderQueue.GetItems())
        {
            m_device->Draw(item);
        }
        m_device->EndFrame();
        m_device->Present();

        m_lastDrawCalls = m_renderQueue.GetItems().size();
        if (m_logger != nullptr)
        {
            m_logger->Debug("RenderFrame draw calls: " + std::to_string(m_lastDrawCalls));
        }
    }

    std::size_t Renderer::GetLastDrawCallCount() const
    {
        return m_lastDrawCalls;
    }
}
