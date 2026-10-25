// 文件：include/tinyengine/runtime/fixed_step.h
// 作用：累计固定步长时间并计算单帧允许执行的物理更新次数。
// 约定：超过调用方补步预算的时间债直接丢弃，剩余小于一步的时间继续保留。
// 类型：公开声明。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tinyengine
{
class FixedStepAccumulator
{
  public:
    /// @brief 累计一帧时间并消费本帧可执行的固定步数。
    /// @param deltaTime 本帧仿真时间，单位为秒；非正值或非有限值不推进。
    /// @param fixedStep 每次物理更新的秒数，必须为正的有限值。
    /// @param maximumSteps 单帧最多执行的次数；零表示不推进。
    /// @return 本帧应执行的固定更新次数，不超过 maximumSteps。
    std::uint32_t advance(float deltaTime, float fixedStep, std::uint32_t maximumSteps)
    {
        if (!(deltaTime > 0.0f) || !std::isfinite(deltaTime) || !(fixedStep > 0.0f) || !std::isfinite(fixedStep) ||
            maximumSteps == 0)
            return 0;
        const double step = static_cast<double>(fixedStep);
        m_remaining = std::min(m_remaining + static_cast<double>(deltaTime), step * static_cast<double>(maximumSteps));
        // 先计算整数步数再一次性扣除；逐次 float 相减会把最后一步舍入成“不够一步”。
        const auto steps = static_cast<std::uint32_t>(m_remaining / step);
        m_remaining -= step * static_cast<double>(steps);
        return steps;
    }

    /// @brief 清空跨帧保留的时间；用于开始或结束一次仿真会话。
    void reset()
    {
        m_remaining = 0.0;
    }

  private:
    double m_remaining = 0.0;
};
} // namespace tinyengine
