// 文件：include/MiniEngine/Input/InputSystem.hpp
// 作用：统一键鼠、手柄、输入映射、运行时重绑定和设备热插拔。
// 约定：原始设备事件先归一化，再通过映射资产转换为游戏动作。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Render/Renderer.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
enum class InputGlyphSet
{
    KeyboardMouse,
    Xbox,
    PlayStation,
    Nintendo
};

class InputSystem
{
  public:
    void Update(const InputState &state);
    float GetAxis(const std::string &name) const;
    bool IsDown(const std::string &name) const;
    bool WasPressed(const std::string &name) const;
    void BindAxis(const std::string &name, const std::string &source, float scale = 1.0f);
    void BindAction(const std::string &name, const std::string &source);
    void ClearBindings();
    void ResetDefaultBindings();
    bool LoadBindings(const std::string &path);
    bool SaveBindings(const std::string &path) const;
    static std::string GetGlyph(const std::string &source, InputGlyphSet set);
    void SetHapticCallback(std::function<bool(float, float, std::uint32_t)> callback);
    bool Rumble(float lowFrequency, float highFrequency, std::uint32_t durationMilliseconds) const;

  private:
    struct AxisBinding
    {
        std::string source;
        float scale = 1.0f;
    };
    std::unordered_map<std::string, float> m_axes;
    std::unordered_map<std::string, bool> m_actions;
    std::unordered_map<std::string, bool> m_previousActions;
    std::unordered_map<std::string, std::vector<AxisBinding>> m_axisBindings;
    std::unordered_map<std::string, std::vector<std::string>> m_actionBindings;
    bool m_defaultsInitialized = false;
    std::function<bool(float, float, std::uint32_t)> m_hapticCallback;
};
} // namespace MiniEngine
