// 文件：include/tinyengine/math/math.h
// 作用：定义向量、矩阵、四元数、包围体及常用几何运算。
// 约定：数学类型采用列主序约定；跨渲染与物理边界时需保持坐标系一致。
// 类型：公开声明。

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace tinyengine
{
constexpr float kPi = 3.14159265358979323846f;
/// @brief 把角度值转换为弧度值。
/// @param degrees 角度值，单位为度。
/// @return 返回计算得到的数值、数量或索引。
constexpr float degreesToRadians(const float degrees)
{
    return degrees * kPi / 180.0f;
}

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    /// @brief 实现 `+` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 operator+(const Vec3& rhs) const
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    /// @brief 实现 `-` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 operator-(const Vec3& rhs) const
    {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    /// @brief 实现 `*` 运算符语义。
    /// @param scalar 标量乘数或除数。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 operator*(const float scalar) const
    {
        return {x * scalar, y * scalar, z * scalar};
    }
    /// @brief 实现 `/` 运算符语义。
    /// @param scalar 标量乘数或除数。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 operator/(const float scalar) const
    {
        return {x / scalar, y / scalar, z / scalar};
    }
    /// @brief 实现 `+=` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    Vec3& operator+=(const Vec3& rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    /// @brief 实现 `-=` 运算符语义。
    /// @param rhs 右侧运算对象。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    Vec3& operator-=(const Vec3& rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
};

/// @brief 计算两个向量的点积。
/// @param lhs 左侧运算对象。
/// @param rhs 右侧运算对象。
/// @return 返回计算得到的数值、数量或索引。
inline float dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

/// @brief 计算两个向量的叉积。
/// @param lhs 左侧运算对象。
/// @param rhs 右侧运算对象。
/// @return 返回接口计算或查询得到的结果。
inline Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}

/// @brief 计算向量长度的平方。
/// @param value 要写入、比较或计算的值。
/// @return 返回计算得到的数值、数量或索引。
inline float lengthSquared(const Vec3& value)
{
    return dot(value, value);
}
/// @brief 计算向量长度。
/// @param value 要写入、比较或计算的值。
/// @return 返回计算得到的数值、数量或索引。
inline float length(const Vec3& value)
{
    return std::sqrt(lengthSquared(value));
}
/// @brief 将输入值规范化。
/// @param value 要写入、比较或计算的值。
/// @return 返回接口计算或查询得到的结果。
inline Vec3 normalize(const Vec3& value)
{
    const float magnitude = length(value);
    return magnitude > std::numeric_limits<float>::epsilon() ? value / magnitude : Vec3{};
}

struct Vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Quaternion
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    /// @brief 根据欧拉角度创建四元数。
    /// @param euler 旋转值，单位与空间由接口约定。
    /// @return 返回接口计算或查询得到的结果。
    static Quaternion fromEulerDegrees(const Vec3& euler)
    {
        const float hx = degreesToRadians(euler.x) * 0.5f;
        const float hy = degreesToRadians(euler.y) * 0.5f;
        const float hz = degreesToRadians(euler.z) * 0.5f;
        const float cx = std::cos(hx), sx = std::sin(hx);
        const float cy = std::cos(hy), sy = std::sin(hy);
        const float cz = std::cos(hz), sz = std::sin(hz);
        return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
                cx * cy * cz + sx * sy * sz};
    }
};

struct Mat4
{
    std::array<float, 16> data{};

    /// @brief 创建单位矩阵。
    /// @return 返回接口计算或查询得到的结果。
    static Mat4 identity()
    {
        Mat4 result;
        result.data = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        return result;
    }

    /// @brief 访问对象保存的底层数据。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const float* Data() const
    {
        return data.data();
    }
    /// @brief 访问指定行列或索引处的元素。
    /// @param row 矩阵行索引。
    /// @param column 矩阵列索引。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    float& at(const std::size_t row, const std::size_t column)
    {
        return data[column * 4 + row];
    }
    /// @brief 访问指定行列或索引处的元素。
    /// @param row 矩阵行索引。
    /// @param column 矩阵列索引。
    /// @return 返回计算得到的数值、数量或索引。
    float at(const std::size_t row, const std::size_t column) const
    {
        return data[column * 4 + row];
    }
};

/// @brief 实现 `*` 运算符语义。
/// @param lhs 左侧运算对象。
/// @param rhs 右侧运算对象。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 operator*(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t index = 0; index < 4; ++index)
                result.at(row, column) += lhs.at(row, index) * rhs.at(index, column);
    return result;
}

/// @brief 创建平移矩阵。
/// @param translation 平移向量。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 translationMatrix(const Vec3& translation)
{
    Mat4 result = Mat4::identity();
    result.at(0, 3) = translation.x;
    result.at(1, 3) = translation.y;
    result.at(2, 3) = translation.z;
    return result;
}

/// @brief 创建缩放矩阵。
/// @param scale 缩放比例或缩放向量。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 scaleMatrix(const Vec3& scale)
{
    Mat4 result = Mat4::identity();
    result.at(0, 0) = scale.x;
    result.at(1, 1) = scale.y;
    result.at(2, 2) = scale.z;
    return result;
}

/// @brief 根据四元数创建旋转矩阵。
/// @param q 接口所需的输入参数。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 rotationMatrix(const Quaternion& q)
{
    Mat4 result = Mat4::identity();
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    result.at(0, 0) = 1.0f - 2.0f * (yy + zz);
    result.at(1, 0) = 2.0f * (xy + wz);
    result.at(2, 0) = 2.0f * (xz - wy);
    result.at(0, 1) = 2.0f * (xy - wz);
    result.at(1, 1) = 1.0f - 2.0f * (xx + zz);
    result.at(2, 1) = 2.0f * (yz + wx);
    result.at(0, 2) = 2.0f * (xz + wy);
    result.at(1, 2) = 2.0f * (yz - wx);
    result.at(2, 2) = 1.0f - 2.0f * (xx + yy);
    return result;
}

/// @brief 变换 `Matrix`。
/// @param position 世界空间或局部空间位置。
/// @param rotationDegrees 旋转值，单位与空间由接口约定。
/// @param scale 缩放比例或缩放向量。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 transformMatrix(const Vec3& position, const Vec3& rotationDegrees, const Vec3& scale)
{
    return translationMatrix(position) * rotationMatrix(Quaternion::fromEulerDegrees(rotationDegrees)) *
           scaleMatrix(scale);
}

/// @brief 创建透视投影矩阵。
/// @param verticalFovDegrees 本次操作使用的数值。
/// @param aspect 本次操作使用的数值。
/// @param nearPlane 相机近裁剪面距离。
/// @param farPlane 相机远裁剪面距离。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 perspective(const float verticalFovDegrees, const float aspect, const float nearPlane, const float farPlane)
{
    const float f = 1.0f / std::tan(degreesToRadians(verticalFovDegrees) * 0.5f);
    Mat4 result{};
    result.at(0, 0) = f / aspect;
    result.at(1, 1) = f;
    result.at(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.at(2, 3) = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    result.at(3, 2) = -1.0f;
    return result;
}

/// @brief 创建正交投影矩阵。
/// @param left 接口所需的输入参数。
/// @param right 接口所需的输入参数。
/// @param bottom 接口所需的输入参数。
/// @param top 接口所需的输入参数。
/// @param nearPlane 相机近裁剪面距离。
/// @param farPlane 相机远裁剪面距离。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 orthographic(const float left, const float right, const float bottom, const float top,
                         const float nearPlane, const float farPlane)
{
    Mat4 result = Mat4::identity();
    result.at(0, 0) = 2.0f / (right - left);
    result.at(1, 1) = 2.0f / (top - bottom);
    result.at(2, 2) = -2.0f / (farPlane - nearPlane);
    result.at(0, 3) = -(right + left) / (right - left);
    result.at(1, 3) = -(top + bottom) / (top - bottom);
    result.at(2, 3) = -(farPlane + nearPlane) / (farPlane - nearPlane);
    return result;
}

/// @brief 创建朝向目标点的观察矩阵。
/// @param eye 相关坐标或空间位置。
/// @param target 目标数据、目标对象或目标位置。
/// @param up 接口所需的输入参数。
/// @return 返回接口计算或查询得到的结果。
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 correctedUp = cross(right, forward);
    Mat4 result = Mat4::identity();
    result.at(0, 0) = right.x;
    result.at(0, 1) = right.y;
    result.at(0, 2) = right.z;
    result.at(1, 0) = correctedUp.x;
    result.at(1, 1) = correctedUp.y;
    result.at(1, 2) = correctedUp.z;
    result.at(2, 0) = -forward.x;
    result.at(2, 1) = -forward.y;
    result.at(2, 2) = -forward.z;
    result.at(0, 3) = -dot(right, eye);
    result.at(1, 3) = -dot(correctedUp, eye);
    result.at(2, 3) = dot(forward, eye);
    return result;
}

struct Ray
{
    Vec3 origin;
    Vec3 direction{0.0f, 0.0f, -1.0f};
};
struct Plane
{
    Vec3 normal;
    float distance = 0.0f;
};
struct Aabb
{
    Vec3 minimum;
    Vec3 maximum;
    /// @brief 判断两个几何对象是否相交。
    /// @param rhs 右侧运算对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool intersects(const Aabb& rhs) const
    {
        return minimum.x <= rhs.maximum.x && maximum.x >= rhs.minimum.x && minimum.y <= rhs.maximum.y &&
               maximum.y >= rhs.minimum.y && minimum.z <= rhs.maximum.z && maximum.z >= rhs.minimum.z;
    }
};
} // namespace tinyengine
