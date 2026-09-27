// 文件：include/MiniEngine/Types.hpp
// 作用：集中声明引擎通用整数别名、实体句柄和基础常量。
// 约定：这里的别名会进入多数公开 ABI；调整位宽或句柄布局必须同步序列化与插件版本。
// 类型：公开声明。

#pragma once

#include <cstdint>
#include <functional>

namespace MiniEngine
{
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using Entity = u64;
constexpr Entity InvalidEntity = 0;

/// @brief 根据索引和代际号创建实体句柄。
/// @param index 目标元素或骨骼索引。
/// @param generation 接口所需的输入参数。
/// @return 返回实体句柄；失败时返回 `InvalidEntity`。
constexpr Entity MakeEntity(const u32 index, const u32 generation)
{
    return (static_cast<u64>(generation) << 32u) | static_cast<u64>(index);
}

/// @brief 从实体句柄提取索引部分。
/// @param entity 目标实体句柄。
/// @return 返回计算得到的数值、数量或索引。
constexpr u32 EntityIndex(const Entity entity)
{
    return static_cast<u32>(entity & 0xffffffffull);
}

/// @brief 从实体句柄提取代际部分。
/// @param entity 目标实体句柄。
/// @return 返回计算得到的数值、数量或索引。
constexpr u32 EntityGeneration(const Entity entity)
{
    return static_cast<u32>(entity >> 32u);
}

struct Handle
{
    u32 index = 0;
    u32 generation = 0;

    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const
    {
        return generation != 0;
    }

    /// @brief 实现 `==` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool operator==(const Handle &rhs) const
    {
        return index == rhs.index && generation == rhs.generation;
    }
};
} // MiniEngine 命名空间

namespace std
{
template <>
struct hash<MiniEngine::Handle>
{
    /// @brief 实现 `()` 运算符语义。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回计算得到的数值、数量或索引。
    size_t operator()(const MiniEngine::Handle &value) const noexcept
    {
        return (static_cast<size_t>(value.generation) << 32u) ^ value.index;
    }
};
} // std 命名空间
