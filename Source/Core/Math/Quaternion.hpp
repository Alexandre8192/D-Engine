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
    // ---
    // Purpose : POD quaternion (float) used for representing rotations.
    // Contract: Layout {x,y,z,w} with vector part first; trivially copyable; identity defaults to (0,0,0,1).
    // Notes   : All rotation helpers assume unit quaternions; heavy ops implemented out-of-line.
    // ---
    struct Quatf
    {
        float32 x, y, z, w;

        constexpr Quatf() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {} // Identity
        constexpr Quatf(float32 _x, float32 _y, float32 _z, float32 _w) noexcept : x(_x), y(_y), z(_z), w(_w) {}

        static constexpr Quatf Identity() noexcept { return Quatf(0.0f, 0.0f, 0.0f, 1.0f); }
    };

    // ---
    // Purpose : Quaternion arithmetic mirrors vector semantics for ergonomics.
    // Contract: All operators are constexpr/noexcept and avoid heap work; multiplication follows Hamilton product order.
    // Notes   : Negating a quaternion preserves rotation since unit quats have two representations.
    // ---
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

    // ---
    // Purpose : Rotate a 3D vector by a unit quaternion.
    // Contract: Assumes `q` is normalized; non-unit inputs may scale vectors; no allocations.
    // Notes   : Uses `2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)` formulation for stability.
    // ---
    [[nodiscard]] constexpr Vec3f operator*(const Quatf& q, const Vec3f& v) noexcept
    {
        // Rotate vector v by quaternion q
        // Formula: v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
        Vec3f qv(q.x, q.y, q.z);
        Vec3f uv = Cross(qv, v);
        Vec3f uuv = Cross(qv, uv);
        return v + ((uv * q.w) + uuv) * 2.0f;
    }

    // ---
    // Purpose : Core analytic helpers for quaternions (Dot + Normalize).
    // Contract: Dot is inline; Normalize guards against degenerate quats by returning identity fallback.
    // Notes   : Normalization uses Epsilon as safety margin to avoid NaNs.
    // ---
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

    // ---
    // Purpose : Construct a rotation quaternion from an axis/angle pair.
    // Contract: Axis may be any non-zero vector; function normalizes internally and returns unit quaternion; radians expected.
    // Notes   : Right-handed convention with positive angles rotating via right-hand rule.
    // ---
    [[nodiscard]] Quatf FromAxisAngle(const Vec3f& axis, float32 angleRadians) noexcept;

    // ---
    // Purpose : Build a quaternion from intrinsic yaw (Y), pitch (X), roll (Z) angles.
    // Contract: Angles expressed in radians; expects finite inputs; returns unit quaternion.
    // Notes   : Order is Y (yaw) then X (pitch) then Z (roll); aligns with engine camera conventions.
    // ---
    [[nodiscard]] Quatf FromEuler(float32 pitch, float32 yaw, float32 roll) noexcept; // Order: yaw (Y), pitch (X), roll (Z)

    // ---
    // Purpose : Convert a unit quaternion into a column-major rotation matrix.
    // Contract: Assumes normalized input; returns Mat4f with rotation in upper-left 3x3 and identity translation.
    // Notes   : Does not renormalize; callers should Normalize beforehand if needed.
    // ---
    [[nodiscard]] Mat4f ToMatrix(const Quatf& q) noexcept;

    // ---
    // Purpose : Perform spherical linear interpolation between unit quaternions.
    // Contract: `t` expected in [0,1]; normalizes path when inputs nearly aligned; returns unit quaternion.
    // Notes   : Handles shortest-path selection by flipping sign on negative dot products.
    // ---
    [[nodiscard]] Quatf Slerp(const Quatf& a, const Quatf& b, float32 t) noexcept;

    namespace detail
    {
        template <>
        struct LerpPolicy<Quatf>
        {
            static constexpr bool kHasCustomLerp = true;

            // ---
            // Purpose : Route templated Lerp<Quatf> calls to Slerp for proper unit-quaternion interpolation.
            // Contract: Forwards to Slerp with identical inputs; assumes quaternions are unit-length.
            // Notes   : Keeps API symmetric with scalar Lerp while avoiding invalid linear blends.
            // ---
            [[nodiscard]] static Quatf Lerp(const Quatf& a, const Quatf& b, float32 t) noexcept
            {
                return Slerp(a, b, t);
            }
        };
    }

} // namespace dng
