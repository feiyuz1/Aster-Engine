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
    /// @brief 更新对象当前状态。
    /// @param state 目标状态、模式或类型。
    void Update(const InputState &state);
    /// @brief 获取 `Axis`。
    /// @param name 对象名称或注册键。
    /// @return 返回计算得到的数值、数量或索引。
    float GetAxis(const std::string &name) const;
    /// @brief 判断是否满足 `Down` 条件。
    /// @param name 对象名称或注册键。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsDown(const std::string &name) const;
    /// @brief 执行 `WasPressed` 所表示的操作。
    /// @param name 对象名称或注册键。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool WasPressed(const std::string &name) const;
    /// @brief 绑定 `Axis`。
    /// @param name 对象名称或注册键。
    /// @param source 源数据、源资源或源位置。
    /// @param scale 缩放比例或缩放向量。
    void BindAxis(const std::string &name, const std::string &source, float scale = 1.0f);
    /// @brief 绑定 `Action`。
    /// @param name 对象名称或注册键。
    /// @param source 源数据、源资源或源位置。
    void BindAction(const std::string &name, const std::string &source);
    /// @brief 清空 `Bindings`。
    void ClearBindings();
    /// @brief 重置 `DefaultBindings`。
    void ResetDefaultBindings();
    /// @brief 加载 `Bindings`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool LoadBindings(const std::string &path);
    /// @brief 保存 `Bindings`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveBindings(const std::string &path) const;
    /// @brief 获取 `Glyph`。
    /// @param source 源数据、源资源或源位置。
    /// @param set 接口所需的输入参数。
    /// @return 返回对应的文本或路径。
    static std::string GetGlyph(const std::string &source, InputGlyphSet set);
    /// @brief 设置 `HapticCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void SetHapticCallback(std::function<bool(float, float, std::uint32_t)> callback);
    /// @brief 触发手柄震动。
    /// @param lowFrequency 接口所需的输入参数。
    /// @param highFrequency 接口所需的输入参数。
    /// @param durationMilliseconds 时间值，单位按接口约定为秒。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
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
} // 结束 MiniEngine 命名空间
