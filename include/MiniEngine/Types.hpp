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

constexpr Entity MakeEntity(const u32 index, const u32 generation)
{
    return (static_cast<u64>(generation) << 32u) | static_cast<u64>(index);
}

constexpr u32 EntityIndex(const Entity entity)
{
    return static_cast<u32>(entity & 0xffffffffull);
}

constexpr u32 EntityGeneration(const Entity entity)
{
    return static_cast<u32>(entity >> 32u);
}

struct Handle
{
    u32 index = 0;
    u32 generation = 0;

    bool IsValid() const
    {
        return generation != 0;
    }

    bool operator==(const Handle &rhs) const
    {
        return index == rhs.index && generation == rhs.generation;
    }
};
} // namespace MiniEngine

namespace std
{
template <> struct hash<MiniEngine::Handle>
{
    size_t operator()(const MiniEngine::Handle &value) const noexcept
    {
        return (static_cast<size_t>(value.generation) << 32u) ^ value.index;
    }
};
} // namespace std
