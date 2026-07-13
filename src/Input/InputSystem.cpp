#include "MiniEngine/Input/InputSystem.hpp"

namespace MiniEngine
{
    void InputSystem::Update(const InputState& state)
    {
        m_previousActions = m_actions;
        m_axes = {{"MoveX", state.moveX}, {"MoveY", state.moveY}, {"MoveZ", state.moveZ}, {"LookX", state.rotateY + state.mouseX}, {"LookY", state.rotateX + state.mouseY}, {"RotateZ", state.rotateZ}, {"Scale", state.scale}};
        m_actions = {{"Primary", state.primaryAction}, {"Sprint", state.sprint}, {"Quit", state.quitRequested}};
    }
    float InputSystem::GetAxis(const std::string& name) const { const auto found = m_axes.find(name); return found == m_axes.end() ? 0.0f : found->second; }
    bool InputSystem::IsDown(const std::string& name) const { const auto found = m_actions.find(name); return found != m_actions.end() && found->second; }
    bool InputSystem::WasPressed(const std::string& name) const
    {
        const bool current = IsDown(name); const auto found = m_previousActions.find(name); const bool previous = found != m_previousActions.end() && found->second;
        return current && !previous;
    }
}
