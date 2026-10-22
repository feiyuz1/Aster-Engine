// 文件：include/tinyengine/types.h
// 作用：集中声明引擎通用整数别名、实体句柄和基础常量。
// 约定：这里的别名会进入多数公开 ABI；调整位宽或句柄布局必须同步序列化与插件版本。
// 类型：公开声明。

#pragma once

#include <cstdint>
#include <functional>

namespace tinyengine
{

using Entity = std::uint64_t;
constexpr Entity kInvalidEntity = 0;

/// @brief 根据索引和代际号创建实体句柄。
/// @param index 目标元素或骨骼索引。
/// @param generation 接口所需的输入参数。
/// @return 返回实体句柄；失败时返回 `InvalidEntity`。
constexpr Entity makeEntity(const std::uint32_t index, const std::uint32_t generation)
{
    return (static_cast<std::uint64_t>(generation) << 32u) | static_cast<std::uint64_t>(index);
}

/// @brief 从实体句柄提取索引部分。
/// @param entity 目标实体句柄。
/// @return 返回计算得到的数值、数量或索引。
constexpr std::uint32_t entityIndex(const Entity entity)
{
    return static_cast<std::uint32_t>(entity & 0xffffffffull);
}

/// @brief 从实体句柄提取代际部分。
/// @param entity 目标实体句柄。
/// @return 返回计算得到的数值、数量或索引。
constexpr std::uint32_t entityGeneration(const Entity entity)
{
    return static_cast<std::uint32_t>(entity >> 32u);
}

struct Handle
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isValid() const
    {
        return generation != 0;
    }

    /// @brief 实现 `==` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool operator==(const Handle& rhs) const
    {
        return index == rhs.index && generation == rhs.generation;
    }
};
} // namespace tinyengine

namespace std
{
template <> struct hash<tinyengine::Handle>
{
    /// @brief 实现 `()` 运算符语义。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回计算得到的数值、数量或索引。
    size_t operator()(const tinyengine::Handle& value) const noexcept
    {
        return (static_cast<size_t>(value.generation) << 32u) ^ value.index;
    }
};
} // namespace std
