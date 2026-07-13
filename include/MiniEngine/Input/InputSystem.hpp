#pragma once

#include "MiniEngine/Render/Renderer.hpp"

#include <string>
#include <unordered_map>

namespace MiniEngine
{
    class InputSystem
    {
    public:
        void Update(const InputState& state);
        float GetAxis(const std::string& name) const;
        bool IsDown(const std::string& name) const;
        bool WasPressed(const std::string& name) const;
    private:
        std::unordered_map<std::string, float> m_axes;
        std::unordered_map<std::string, bool> m_actions;
        std::unordered_map<std::string, bool> m_previousActions;
    };
}
