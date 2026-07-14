#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace MiniEngine
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float DegreesToRadians(const float degrees)
{
    return degrees * Pi / 180.0f;
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

    Vec3 operator+(const Vec3 &rhs) const
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    Vec3 operator-(const Vec3 &rhs) const
    {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    Vec3 operator*(const float scalar) const
    {
        return {x * scalar, y * scalar, z * scalar};
    }
    Vec3 operator/(const float scalar) const
    {
        return {x / scalar, y / scalar, z / scalar};
    }
    Vec3 &operator+=(const Vec3 &rhs)
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    Vec3 &operator-=(const Vec3 &rhs)
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
};

inline float Dot(const Vec3 &lhs, const Vec3 &rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 Cross(const Vec3 &lhs, const Vec3 &rhs)
{
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}

inline float LengthSquared(const Vec3 &value)
{
    return Dot(value, value);
}
inline float Length(const Vec3 &value)
{
    return std::sqrt(LengthSquared(value));
}
inline Vec3 Normalize(const Vec3 &value)
{
    const float length = Length(value);
    return length > std::numeric_limits<float>::epsilon() ? value / length : Vec3{};
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

    static Quaternion FromEulerDegrees(const Vec3 &euler)
    {
        const float hx = DegreesToRadians(euler.x) * 0.5f;
        const float hy = DegreesToRadians(euler.y) * 0.5f;
        const float hz = DegreesToRadians(euler.z) * 0.5f;
        const float cx = std::cos(hx), sx = std::sin(hx);
        const float cy = std::cos(hy), sy = std::sin(hy);
        const float cz = std::cos(hz), sz = std::sin(hz);
        return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz};
    }
};

struct Mat4
{
    std::array<float, 16> data{};

    static Mat4 Identity()
    {
        Mat4 result;
        result.data = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        return result;
    }

    const float *Data() const
    {
        return data.data();
    }
    float &At(const std::size_t row, const std::size_t column)
    {
        return data[column * 4 + row];
    }
    float At(const std::size_t row, const std::size_t column) const
    {
        return data[column * 4 + row];
    }
};

inline Mat4 operator*(const Mat4 &lhs, const Mat4 &rhs)
{
    Mat4 result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t index = 0; index < 4; ++index)
                result.At(row, column) += lhs.At(row, index) * rhs.At(index, column);
    return result;
}

inline Mat4 TranslationMatrix(const Vec3 &translation)
{
    Mat4 result = Mat4::Identity();
    result.At(0, 3) = translation.x;
    result.At(1, 3) = translation.y;
    result.At(2, 3) = translation.z;
    return result;
}

inline Mat4 ScaleMatrix(const Vec3 &scale)
{
    Mat4 result = Mat4::Identity();
    result.At(0, 0) = scale.x;
    result.At(1, 1) = scale.y;
    result.At(2, 2) = scale.z;
    return result;
}

inline Mat4 RotationMatrix(const Quaternion &q)
{
    Mat4 result = Mat4::Identity();
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    result.At(0, 0) = 1.0f - 2.0f * (yy + zz);
    result.At(1, 0) = 2.0f * (xy + wz);
    result.At(2, 0) = 2.0f * (xz - wy);
    result.At(0, 1) = 2.0f * (xy - wz);
    result.At(1, 1) = 1.0f - 2.0f * (xx + zz);
    result.At(2, 1) = 2.0f * (yz + wx);
    result.At(0, 2) = 2.0f * (xz + wy);
    result.At(1, 2) = 2.0f * (yz - wx);
    result.At(2, 2) = 1.0f - 2.0f * (xx + yy);
    return result;
}

inline Mat4 TransformMatrix(const Vec3 &position, const Vec3 &rotationDegrees, const Vec3 &scale)
{
    return TranslationMatrix(position) * RotationMatrix(Quaternion::FromEulerDegrees(rotationDegrees)) * ScaleMatrix(scale);
}

inline Mat4 Perspective(const float verticalFovDegrees, const float aspect, const float nearPlane, const float farPlane)
{
    const float f = 1.0f / std::tan(DegreesToRadians(verticalFovDegrees) * 0.5f);
    Mat4 result{};
    result.At(0, 0) = f / aspect;
    result.At(1, 1) = f;
    result.At(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.At(2, 3) = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    result.At(3, 2) = -1.0f;
    return result;
}

inline Mat4 Orthographic(const float left, const float right, const float bottom, const float top, const float nearPlane,
                         const float farPlane)
{
    Mat4 result = Mat4::Identity();
    result.At(0, 0) = 2.0f / (right - left);
    result.At(1, 1) = 2.0f / (top - bottom);
    result.At(2, 2) = -2.0f / (farPlane - nearPlane);
    result.At(0, 3) = -(right + left) / (right - left);
    result.At(1, 3) = -(top + bottom) / (top - bottom);
    result.At(2, 3) = -(farPlane + nearPlane) / (farPlane - nearPlane);
    return result;
}

inline Mat4 LookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
{
    const Vec3 forward = Normalize(target - eye);
    const Vec3 right = Normalize(Cross(forward, up));
    const Vec3 correctedUp = Cross(right, forward);
    Mat4 result = Mat4::Identity();
    result.At(0, 0) = right.x;
    result.At(0, 1) = right.y;
    result.At(0, 2) = right.z;
    result.At(1, 0) = correctedUp.x;
    result.At(1, 1) = correctedUp.y;
    result.At(1, 2) = correctedUp.z;
    result.At(2, 0) = -forward.x;
    result.At(2, 1) = -forward.y;
    result.At(2, 2) = -forward.z;
    result.At(0, 3) = -Dot(right, eye);
    result.At(1, 3) = -Dot(correctedUp, eye);
    result.At(2, 3) = Dot(forward, eye);
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
    bool Intersects(const Aabb &rhs) const
    {
        return minimum.x <= rhs.maximum.x && maximum.x >= rhs.minimum.x && minimum.y <= rhs.maximum.y && maximum.y >= rhs.minimum.y &&
               minimum.z <= rhs.maximum.z && maximum.z >= rhs.minimum.z;
    }
};
} // namespace MiniEngine
