#pragma once
// ============================================================================
// D-Engine - Source/Core/Math/Quaternion.hpp
// ----------------------------------------------------------------------------
// Purpose : Concrete quaternion type (Quatf) and operations.
// Contract: Unit quaternions expected for rotation.
//           Layout: x, y, z, w (vector part first).
// Notes   : Float-first implementation. Heavy ops are out-of-line.
// ============================================================================

#include "Core/Math/Vector.hpp"
#include "Core/Math/Matrix.hpp"

namespace dng
{
    // ------------------------------------------------------------------------
    // Quatf
    // ------------------------------------------------------------------------
    struct Quatf
    {
        float32 x, y, z, w;

        constexpr Quatf() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {} // Identity
        constexpr Quatf(float32 _x, float32 _y, float32 _z, float32 _w) noexcept : x(_x), y(_y), z(_z), w(_w) {}

        static constexpr Quatf Identity() noexcept { return Quatf(0.0f, 0.0f, 0.0f, 1.0f); }
    };

    [[nodiscard]] constexpr Quatf operator-(const Quatf& q) noexcept
    {
        return Quatf(-q.x, -q.y, -q.z, -q.w);
    }

    [[nodiscard]] constexpr Quatf operator+(const Quatf& a, const Quatf& b) noexcept
    {
        return Quatf(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    }

    [[nodiscard]] constexpr Quatf operator-(const Quatf& a, const Quatf& b) noexcept
    {
        return Quatf(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    }

    [[nodiscard]] constexpr Quatf operator*(const Quatf& a, float32 s) noexcept
    {
        return Quatf(a.x * s, a.y * s, a.z * s, a.w * s);
    }

    [[nodiscard]] constexpr Quatf operator*(const Quatf& a, const Quatf& b) noexcept
    {
        // Standard quaternion multiplication
        return Quatf(
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        );
    }

    [[nodiscard]] constexpr Vec3f operator*(const Quatf& q, const Vec3f& v) noexcept
    {
        // Rotate vector v by quaternion q
        // Formula: v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
        Vec3f qv(q.x, q.y, q.z);
        Vec3f uv = Cross(qv, v);
        Vec3f uuv = Cross(qv, uv);
        return v + ((uv * q.w) + uuv) * 2.0f;
    }

    [[nodiscard]] inline float32 Dot(const Quatf& a, const Quatf& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    [[nodiscard]] inline Quatf Normalize(const Quatf& q) noexcept
    {
        float32 lenSq = Dot(q, q);
        if (lenSq > Epsilon)
            return q * (1.0f / Sqrt(lenSq));
        return Quatf::Identity();
    }

    // ------------------------------------------------------------------------
    // Heavy Operations (Implemented in Math.cpp)
    // ------------------------------------------------------------------------

    [[nodiscard]] Quatf FromAxisAngle(const Vec3f& axis, float32 angleRadians) noexcept;
    [[nodiscard]] Quatf FromEuler(float32 pitch, float32 yaw, float32 roll) noexcept; // Order TBD (usually YXZ or ZYX)
    [[nodiscard]] Mat4f ToMatrix(const Quatf& q) noexcept;
    [[nodiscard]] Quatf Slerp(const Quatf& a, const Quatf& b, float32 t) noexcept;

    namespace detail
    {
        template <>
        struct LerpPolicy<Quatf>
        {
            static constexpr bool kHasCustomLerp = true;

            [[nodiscard]] static Quatf Lerp(const Quatf& a, const Quatf& b, float32 t) noexcept
            {
                return Slerp(a, b, t);
            }
        };
    }

} // namespace dng
