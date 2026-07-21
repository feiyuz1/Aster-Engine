// 文件：src/Input/InputSystem.cpp
// 作用：统一键鼠、手柄、输入映射、运行时重绑定和设备热插拔。
// 约定：原始设备事件先归一化，再通过映射资产转换为游戏动作。
// 类型：实现文件。

#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace MiniEngine
{
void InputSystem::Update(const InputState &state)
{
    if (!m_defaultsInitialized)
        ResetDefaultBindings();
    m_previousActions = m_actions;
    const std::unordered_map<std::string, float> physicalAxes = {
        {"MoveX", state.moveX},     {"MoveY", state.moveY},     {"MoveZ", state.moveZ},
        {"RotateX", state.rotateX}, {"RotateY", state.rotateY}, {"RotateZ", state.rotateZ},
        {"MouseX", state.mouseX},   {"MouseY", state.mouseY},   {"Scale", state.scale}};
    const std::unordered_map<std::string, bool> physicalActions = {
        {"Primary", state.primaryAction}, {"Sprint", state.sprint},         {"Jump", state.jump},
        {"Quit", state.quitRequested},    {"UINext", state.uiNavigateNext}, {"UIAccept", state.uiAccept}};
    m_axes.clear();
    for (const auto &pair : m_axisBindings)
        for (const auto &binding : pair.second)
            if (const auto found = physicalAxes.find(binding.source); found != physicalAxes.end())
                m_axes[pair.first] += found->second * binding.scale;
    m_actions.clear();
    for (const auto &pair : m_actionBindings)
        for (const auto &source : pair.second)
            if (const auto found = physicalActions.find(source); found != physicalActions.end())
                m_actions[pair.first] = m_actions[pair.first] || found->second;
}
float InputSystem::GetAxis(const std::string &name) const
{
    const auto found = m_axes.find(name);
    return found == m_axes.end() ? 0.0f : found->second;
}
bool InputSystem::IsDown(const std::string &name) const
{
    const auto found = m_actions.find(name);
    return found != m_actions.end() && found->second;
}
bool InputSystem::WasPressed(const std::string &name) const
{
    const bool current = IsDown(name);
    const auto found = m_previousActions.find(name);
    const bool previous = found != m_previousActions.end() && found->second;
    return current && !previous;
}

void InputSystem::BindAxis(const std::string &name, const std::string &source, const float scale)
{
    m_defaultsInitialized = true;
    m_axisBindings[name].push_back({source, scale});
}

void InputSystem::BindAction(const std::string &name, const std::string &source)
{
    m_defaultsInitialized = true;
    m_actionBindings[name].push_back(source);
}

void InputSystem::ClearBindings()
{
    m_axisBindings.clear();
    m_actionBindings.clear();
    m_axes.clear();
    m_actions.clear();
    m_previousActions.clear();
    m_defaultsInitialized = true;
}

void InputSystem::ResetDefaultBindings()
{
    ClearBindings();
    BindAxis("MoveX", "MoveX");
    BindAxis("MoveY", "MoveY");
    BindAxis("MoveZ", "MoveZ");
    BindAxis("LookX", "RotateY");
    BindAxis("LookX", "MouseX");
    BindAxis("LookY", "RotateX");
    BindAxis("LookY", "MouseY");
    BindAxis("RotateZ", "RotateZ");
    BindAxis("Scale", "Scale");
    BindAction("Primary", "Primary");
    BindAction("Sprint", "Sprint");
    BindAction("Jump", "Jump");
    BindAction("Quit", "Quit");
    BindAction("UINext", "UINext");
    BindAction("UIAccept", "UIAccept");
}

bool InputSystem::LoadBindings(const std::string &path)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string signature;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINYINPUT" || version != 1)
        return false;
    ClearBindings();
    std::string token;
    while (input >> token)
    {
        std::string name, source;
        if (token == "axis")
        {
            float scale = 1.0f;
            if (!(input >> std::quoted(name) >> std::quoted(source) >> scale))
                return false;
            BindAxis(name, source, scale);
        }
        else if (token == "action")
        {
            if (!(input >> std::quoted(name) >> std::quoted(source)))
                return false;
            BindAction(name, source);
        }
        else
            return false;
    }
    return !m_axisBindings.empty() || !m_actionBindings.empty();
}

bool InputSystem::SaveBindings(const std::string &path) const
{
    std::ostringstream output;
    output << "TINYINPUT 1\n";
    for (const auto &pair : m_axisBindings)
        for (const auto &binding : pair.second)
            output << "axis " << std::quoted(pair.first) << ' ' << std::quoted(binding.source) << ' ' << binding.scale << '\n';
    for (const auto &pair : m_actionBindings)
        for (const auto &source : pair.second)
            output << "action " << std::quoted(pair.first) << ' ' << std::quoted(source) << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}

std::string InputSystem::GetGlyph(const std::string &source, const InputGlyphSet set)
{
    static const std::unordered_map<std::string, std::string> keyboard = {
        {"Primary", "Mouse Left"}, {"Sprint", "Shift"}, {"Jump", "Space"},     {"UINext", "Tab"},    {"UIAccept", "Enter"},
        {"MoveX", "A / D"},        {"MoveY", "W / S"},  {"MouseX", "Mouse X"}, {"MouseY", "Mouse Y"}};
    static const std::unordered_map<std::string, std::string> xbox = {{"Primary", "A"},
                                                                      {"Sprint", "LB"},
                                                                      {"Jump", "A"},
                                                                      {"UINext", "D-Pad Down"},
                                                                      {"UIAccept", "A"},
                                                                      {"MoveX", "Left Stick X"},
                                                                      {"MoveY", "Left Stick Y"},
                                                                      {"RotateX", "Right Stick Y"},
                                                                      {"RotateY", "Right Stick X"}};
    static const std::unordered_map<std::string, std::string> playStation = {
        {"Primary", "Cross"}, {"Sprint", "L1"}, {"Jump", "Cross"}, {"UINext", "D-Pad Down"}, {"UIAccept", "Cross"}};
    static const std::unordered_map<std::string, std::string> nintendo = {
        {"Primary", "B"}, {"Sprint", "L"}, {"Jump", "B"}, {"UINext", "D-Pad Down"}, {"UIAccept", "A"}};
    const auto &glyphs = set == InputGlyphSet::KeyboardMouse ? keyboard
                         : set == InputGlyphSet::PlayStation ? playStation
                         : set == InputGlyphSet::Nintendo    ? nintendo
                                                             : xbox;
    const auto found = glyphs.find(source);
    if (found != glyphs.end())
        return found->second;
    if (set != InputGlyphSet::KeyboardMouse)
    {
        const auto fallback = xbox.find(source);
        if (fallback != xbox.end())
            return fallback->second;
    }
    return source;
}

void InputSystem::SetHapticCallback(std::function<bool(float, float, std::uint32_t)> callback)
{
    m_hapticCallback = std::move(callback);
}

bool InputSystem::Rumble(const float lowFrequency, const float highFrequency, const std::uint32_t durationMilliseconds) const
{
    return m_hapticCallback &&
           m_hapticCallback(std::clamp(lowFrequency, 0.0f, 1.0f), std::clamp(highFrequency, 0.0f, 1.0f), durationMilliseconds);
}
} // namespace MiniEngine
