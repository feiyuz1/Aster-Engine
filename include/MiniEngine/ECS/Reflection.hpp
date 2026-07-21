// 文件：include/MiniEngine/ECS/Reflection.hpp
// 作用：登记组件字段元数据，供序列化器和 Inspector 共用。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：公开声明。

#pragma once

#include "MiniEngine/ECS/ECS.hpp"

#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
enum class ReflectedPropertyType
{
    Bool,
    UInt,
    Entity,
    Float,
    Vec3,
    String,
    Enum
};

struct ReflectedProperty
{
    std::string name;
    ReflectedPropertyType type = ReflectedPropertyType::String;
    std::function<void *(World &, Entity)> access;
    std::function<int(const World &, Entity)> getEnum;
    std::function<void(World &, Entity, int)> setEnum;
    std::vector<std::string> enumNames;
    std::string assetType;
    float speed = 0.1f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    bool hasRange = false;
    bool color = false;
};

struct ReflectedComponent
{
    std::string name;
    bool removable = true;
    std::function<bool(const World &, Entity)> has;
    std::function<void(World &, Entity)> add;
    std::function<void(World &, Entity)> remove;
    std::vector<ReflectedProperty> properties;
};

class ReflectionRegistry
{
  public:
    static const ReflectionRegistry &Get();
    const std::vector<ReflectedComponent> &GetComponents() const;
    const ReflectedComponent *FindComponent(const std::string &name) const;

  private:
    ReflectionRegistry();
    std::vector<ReflectedComponent> m_components;
};
} // namespace MiniEngine
