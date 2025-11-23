#pragma once
// ============================================================================
// D-Engine - Source/Core/Math/Vector.hpp
// ----------------------------------------------------------------------------
// Purpose : Concrete vector types (Vec2f, Vec3f, Vec4f) and operations.
// Contract: POD types. Inline operators.
// Notes   : Float-first implementation. No templates for main types.
// ============================================================================

#include "Core/Math/Math.hpp"

namespace dng
{
    // ------------------------------------------------------------------------
    // Vec2f
    // ------------------------------------------------------------------------
    struct Vec2f
    {
        float32 x, y;

        constexpr Vec2f() noexcept : x(0.0f), y(0.0f) {}
        constexpr Vec2f(float32 s) noexcept : x(s), y(s) {}
        constexpr Vec2f(float32 _x, float32 _y) noexcept : x(_x), y(_y) {}

        [[nodiscard]] constexpr Vec2f operator-() const noexcept { return Vec2f(-x, -y); }

        constexpr Vec2f& operator+=(const Vec2f& rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
        constexpr Vec2f& operator-=(const Vec2f& rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
        constexpr Vec2f& operator*=(float32 s) noexcept { x *= s; y *= s; return *this; }
        constexpr Vec2f& operator/=(float32 s) noexcept { float32 inv = 1.0f / s; x *= inv; y *= inv; return *this; }
    };

    [[nodiscard]] constexpr Vec2f operator+(Vec2f lhs, const Vec2f& rhs) noexcept { return lhs += rhs; }
    [[nodiscard]] constexpr Vec2f operator-(Vec2f lhs, const Vec2f& rhs) noexcept { return lhs -= rhs; }
    [[nodiscard]] constexpr Vec2f operator*(Vec2f lhs, float32 s) noexcept { return lhs *= s; }
    [[nodiscard]] constexpr Vec2f operator*(float32 s, Vec2f rhs) noexcept { return rhs *= s; }
    [[nodiscard]] constexpr Vec2f operator*(const Vec2f& lhs, const Vec2f& rhs) noexcept { return Vec2f(lhs.x * rhs.x, lhs.y * rhs.y); }
    [[nodiscard]] constexpr Vec2f operator/(Vec2f lhs, float32 s) noexcept { return lhs /= s; }

    [[nodiscard]] constexpr bool operator==(const Vec2f& lhs, const Vec2f& rhs) noexcept { return lhs.x == rhs.x && lhs.y == rhs.y; }
    [[nodiscard]] constexpr bool operator!=(const Vec2f& lhs, const Vec2f& rhs) noexcept { return !(lhs == rhs); }

    [[nodiscard]] constexpr float32 Dot(const Vec2f& a, const Vec2f& b) noexcept { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] inline float32 LengthSquared(const Vec2f& v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline float32 Length(const Vec2f& v) noexcept { return Sqrt(LengthSquared(v)); }
    [[nodiscard]] inline Vec2f Normalize(const Vec2f& v) noexcept
    {
        float32 lenSq = LengthSquared(v);
        if (lenSq > Epsilon)
            return v * (1.0f / Sqrt(lenSq));
        return Vec2f(0.0f);
    }

    // ------------------------------------------------------------------------
    // Vec3f
    // ------------------------------------------------------------------------
    struct Vec3f
    {
        float32 x, y, z;

        constexpr Vec3f() noexcept : x(0.0f), y(0.0f), z(0.0f) {}
        constexpr Vec3f(float32 s) noexcept : x(s), y(s), z(s) {}
        constexpr Vec3f(float32 _x, float32 _y, float32 _z) noexcept : x(_x), y(_y), z(_z) {}
        constexpr Vec3f(const Vec2f& v, float32 _z) noexcept : x(v.x), y(v.y), z(_z) {}

        [[nodiscard]] constexpr Vec3f operator-() const noexcept { return Vec3f(-x, -y, -z); }

        constexpr Vec3f& operator+=(const Vec3f& rhs) noexcept { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
        constexpr Vec3f& operator-=(const Vec3f& rhs) noexcept { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
        constexpr Vec3f& operator*=(float32 s) noexcept { x *= s; y *= s; z *= s; return *this; }
        constexpr Vec3f& operator/=(float32 s) noexcept { float32 inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }
    };

    [[nodiscard]] constexpr Vec3f operator+(Vec3f lhs, const Vec3f& rhs) noexcept { return lhs += rhs; }
    [[nodiscard]] constexpr Vec3f operator-(Vec3f lhs, const Vec3f& rhs) noexcept { return lhs -= rhs; }
    [[nodiscard]] constexpr Vec3f operator*(Vec3f lhs, float32 s) noexcept { return lhs *= s; }
    [[nodiscard]] constexpr Vec3f operator*(float32 s, Vec3f rhs) noexcept { return rhs *= s; }
    [[nodiscard]] constexpr Vec3f operator*(const Vec3f& lhs, const Vec3f& rhs) noexcept { return Vec3f(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z); }
    [[nodiscard]] constexpr Vec3f operator/(Vec3f lhs, float32 s) noexcept { return lhs /= s; }

    [[nodiscard]] constexpr bool operator==(const Vec3f& lhs, const Vec3f& rhs) noexcept { return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z; }
    [[nodiscard]] constexpr bool operator!=(const Vec3f& lhs, const Vec3f& rhs) noexcept { return !(lhs == rhs); }

    [[nodiscard]] constexpr float32 Dot(const Vec3f& a, const Vec3f& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    [[nodiscard]] constexpr Vec3f Cross(const Vec3f& a, const Vec3f& b) noexcept
    {
        return Vec3f(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }
    [[nodiscard]] inline float32 LengthSquared(const Vec3f& v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline float32 Length(const Vec3f& v) noexcept { return Sqrt(LengthSquared(v)); }
    [[nodiscard]] inline Vec3f Normalize(const Vec3f& v) noexcept
    {
        float32 lenSq = LengthSquared(v);
        if (lenSq > Epsilon)
            return v * (1.0f / Sqrt(lenSq));
        return Vec3f(0.0f);
    }

    // ------------------------------------------------------------------------
    // Vec4f
    // ------------------------------------------------------------------------
    struct Vec4f
    {
        float32 x, y, z, w;

        constexpr Vec4f() noexcept : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        constexpr Vec4f(float32 s) noexcept : x(s), y(s), z(s), w(s) {}
        constexpr Vec4f(float32 _x, float32 _y, float32 _z, float32 _w) noexcept : x(_x), y(_y), z(_z), w(_w) {}
        constexpr Vec4f(const Vec3f& v, float32 _w) noexcept : x(v.x), y(v.y), z(v.z), w(_w) {}

        [[nodiscard]] constexpr Vec4f operator-() const noexcept { return Vec4f(-x, -y, -z, -w); }

        constexpr Vec4f& operator+=(const Vec4f& rhs) noexcept { x += rhs.x; y += rhs.y; z += rhs.z; w += rhs.w; return *this; }
        constexpr Vec4f& operator-=(const Vec4f& rhs) noexcept { x -= rhs.x; y -= rhs.y; z -= rhs.z; w -= rhs.w; return *this; }
        constexpr Vec4f& operator*=(float32 s) noexcept { x *= s; y *= s; z *= s; w *= s; return *this; }
        constexpr Vec4f& operator/=(float32 s) noexcept { float32 inv = 1.0f / s; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }
    };

    [[nodiscard]] constexpr Vec4f operator+(Vec4f lhs, const Vec4f& rhs) noexcept { return lhs += rhs; }
    [[nodiscard]] constexpr Vec4f operator-(Vec4f lhs, const Vec4f& rhs) noexcept { return lhs -= rhs; }
    [[nodiscard]] constexpr Vec4f operator*(Vec4f lhs, float32 s) noexcept { return lhs *= s; }
    [[nodiscard]] constexpr Vec4f operator*(float32 s, Vec4f rhs) noexcept { return rhs *= s; }
    [[nodiscard]] constexpr Vec4f operator*(const Vec4f& lhs, const Vec4f& rhs) noexcept { return Vec4f(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w); }
    [[nodiscard]] constexpr Vec4f operator/(Vec4f lhs, float32 s) noexcept { return lhs /= s; }

    [[nodiscard]] constexpr bool operator==(const Vec4f& lhs, const Vec4f& rhs) noexcept { return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w; }
    [[nodiscard]] constexpr bool operator!=(const Vec4f& lhs, const Vec4f& rhs) noexcept { return !(lhs == rhs); }

    [[nodiscard]] constexpr float32 Dot(const Vec4f& a, const Vec4f& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
    [[nodiscard]] inline float32 LengthSquared(const Vec4f& v) noexcept { return Dot(v, v); }
    [[nodiscard]] inline float32 Length(const Vec4f& v) noexcept { return Sqrt(LengthSquared(v)); }
    [[nodiscard]] inline Vec4f Normalize(const Vec4f& v) noexcept
    {
        float32 lenSq = LengthSquared(v);
        if (lenSq > Epsilon)
            return v * (1.0f / Sqrt(lenSq));
        return Vec4f(0.0f);
    }

} // namespace dng
