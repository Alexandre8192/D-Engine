// ============================================================================
// D-Engine - Source/Core/Math/Math.cpp
// ----------------------------------------------------------------------------
// Purpose : Implementation of heavy math operations.
// ============================================================================

#include "Core/Math/Math.hpp"
#include "Core/Math/Vector.hpp"
#include "Core/Math/Matrix.hpp"
#include "Core/Math/Quaternion.hpp"
#include <cmath>

namespace dng
{
    // ------------------------------------------------------------------------
    // Matrix Operations
    // ------------------------------------------------------------------------

    Mat4f Transpose(const Mat4f& m) noexcept
    {
        Mat4f res;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                res.m[r][c] = m.m[c][r];
        return res;
    }

    Mat4f Inverse(const Mat4f& m) noexcept
    {
#if defined(DNG_ASSERT)
        // Debug: verify that the input matrix does not contain NaN or Inf.
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                DNG_ASSERT(IsFinite(m.m[c][r]) && "Mat4f::Inverse called with non-finite element.");
            }
        }
#endif
        // Standard 4x4 inversion using cofactors
        float32 coef00 = m.m[2][2] * m.m[3][3] - m.m[3][2] * m.m[2][3];
        float32 coef02 = m.m[1][2] * m.m[3][3] - m.m[3][2] * m.m[1][3];
        float32 coef03 = m.m[1][2] * m.m[2][3] - m.m[2][2] * m.m[1][3];

        float32 coef04 = m.m[2][1] * m.m[3][3] - m.m[3][1] * m.m[2][3];
        float32 coef06 = m.m[1][1] * m.m[3][3] - m.m[3][1] * m.m[1][3];
        float32 coef07 = m.m[1][1] * m.m[2][3] - m.m[2][1] * m.m[1][3];

        float32 coef08 = m.m[2][1] * m.m[3][2] - m.m[3][1] * m.m[2][2];
        float32 coef10 = m.m[1][1] * m.m[3][2] - m.m[3][1] * m.m[1][2];
        float32 coef11 = m.m[1][1] * m.m[2][2] - m.m[2][1] * m.m[1][2];

        float32 coef12 = m.m[2][0] * m.m[3][3] - m.m[3][0] * m.m[2][3];
        float32 coef14 = m.m[1][0] * m.m[3][3] - m.m[3][0] * m.m[1][3];
        float32 coef15 = m.m[1][0] * m.m[2][3] - m.m[2][0] * m.m[1][3];

        float32 coef16 = m.m[2][0] * m.m[3][2] - m.m[3][0] * m.m[2][2];
        float32 coef18 = m.m[1][0] * m.m[3][2] - m.m[3][0] * m.m[1][2];
        float32 coef19 = m.m[1][0] * m.m[2][2] - m.m[2][0] * m.m[1][2];

        float32 coef20 = m.m[2][0] * m.m[3][1] - m.m[3][0] * m.m[2][1];
        float32 coef22 = m.m[1][0] * m.m[3][1] - m.m[3][0] * m.m[1][1];
        float32 coef23 = m.m[1][0] * m.m[2][1] - m.m[2][0] * m.m[1][1];

        Vec4f fac0(coef00, coef00, coef02, coef03);
        Vec4f fac1(coef04, coef04, coef06, coef07);
        Vec4f fac2(coef08, coef08, coef10, coef11);
        Vec4f fac3(coef12, coef12, coef14, coef15);
        Vec4f fac4(coef16, coef16, coef18, coef19);
        Vec4f fac5(coef20, coef20, coef22, coef23);

        Vec4f vec0(m.m[1][0], m.m[0][0], m.m[0][0], m.m[0][0]);
        Vec4f vec1(m.m[1][1], m.m[0][1], m.m[0][1], m.m[0][1]);
        Vec4f vec2(m.m[1][2], m.m[0][2], m.m[0][2], m.m[0][2]);
        Vec4f vec3(m.m[1][3], m.m[0][3], m.m[0][3], m.m[0][3]);

        Vec4f inv0(vec1 * fac0 - vec2 * fac1 + vec3 * fac2);
        Vec4f inv1(vec0 * fac0 - vec2 * fac3 + vec3 * fac4);
        Vec4f inv2(vec0 * fac1 - vec1 * fac3 + vec3 * fac5);
        Vec4f inv3(vec0 * fac2 - vec1 * fac4 + vec2 * fac5);

        Vec4f signA(+1, -1, +1, -1);
        Vec4f signB(-1, +1, -1, +1);
        Mat4f inv;
        
        // Columns
        Vec4f c0 = inv0 * signA;
        Vec4f c1 = inv1 * signB;
        Vec4f c2 = inv2 * signA;
        Vec4f c3 = inv3 * signB;

        inv.m[0][0] = c0.x; inv.m[0][1] = c0.y; inv.m[0][2] = c0.z; inv.m[0][3] = c0.w;
        inv.m[1][0] = c1.x; inv.m[1][1] = c1.y; inv.m[1][2] = c1.z; inv.m[1][3] = c1.w;
        inv.m[2][0] = c2.x; inv.m[2][1] = c2.y; inv.m[2][2] = c2.z; inv.m[2][3] = c2.w;
        inv.m[3][0] = c3.x; inv.m[3][1] = c3.y; inv.m[3][2] = c3.z; inv.m[3][3] = c3.w;

        Vec4f row0(inv.m[0][0], inv.m[1][0], inv.m[2][0], inv.m[3][0]);
        Vec4f mRow0(m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3]);
        float32 dot = Dot(mRow0, row0);

        if (Abs(dot) < Epsilon)
        {
            // Singular matrix - return Identity as safe fallback.
            // In debug builds, we still shout loudly.
#if defined(DNG_ASSERT)
            DNG_ASSERT(false && "Mat4f::Inverse called with a singular matrix. Returning identity.");
#endif
            return Mat4f::Identity();
        }

        float32 invDet = 1.0f / dot;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                inv.m[c][r] *= invDet;

        return inv;
    }

    Mat4f LookAt(const Vec3f& eye, const Vec3f& target, const Vec3f& up) noexcept
    {
#if defined(DNG_ASSERT)
        // Basic finite checks
        DNG_ASSERT(IsFinite(eye.x) && IsFinite(eye.y) && IsFinite(eye.z));
        DNG_ASSERT(IsFinite(target.x) && IsFinite(target.y) && IsFinite(target.z));
        DNG_ASSERT(IsFinite(up.x) && IsFinite(up.y) && IsFinite(up.z));

        // Ensure we have a valid forward direction and that "up" is not parallel to it.
        const Vec3f forwardCheck = target - eye;
        const float32 forwardLengthSq = LengthSquared(forwardCheck);
        const Vec3f crossCheck = Cross(forwardCheck, up);
        const float32 crossLengthSq = LengthSquared(crossCheck);

        DNG_ASSERT(forwardLengthSq > Epsilon * Epsilon && "LookAt called with eye and target too close.");
        DNG_ASSERT(crossLengthSq > Epsilon * Epsilon && "LookAt called with nearly parallel up and forward vectors.");
#endif
        // RH LookAt
        // Precondition: 'up' must not be parallel to (target - eye)
        Vec3f f = Normalize(target - eye);
        Vec3f s = Normalize(Cross(f, up));
        Vec3f u = Cross(s, f);

        Mat4f res = Mat4f::Identity();
        res.m[0][0] = s.x;
        res.m[1][0] = s.y;
        res.m[2][0] = s.z;
        res.m[0][1] = u.x;
        res.m[1][1] = u.y;
        res.m[2][1] = u.z;
        res.m[0][2] = -f.x;
        res.m[1][2] = -f.y;
        res.m[2][2] = -f.z;
        res.m[3][0] = -Dot(s, eye);
        res.m[3][1] = -Dot(u, eye);
        res.m[3][2] = Dot(f, eye);
        return res;
    }

    Mat4f Perspective(float32 fovY, float32 aspect, float32 zNear, float32 zFar) noexcept
    {
    #if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(fovY) && IsFinite(aspect) && IsFinite(zNear) && IsFinite(zFar));
        DNG_ASSERT(fovY > 0.0f && fovY < Pi && "Perspective requires 0 < fovY < Pi.");
        DNG_ASSERT(aspect > 0.0f && "Perspective requires positive aspect ratio.");
        DNG_ASSERT(zNear > 0.0f && zFar > zNear && "Perspective requires 0 < zNear < zFar.");
    #endif
        // RH, 0-1 Z range
        // Precondition: zNear != zFar
        const float32 tanHalfFov = Tan(fovY * 0.5f);
        Mat4f res{}; // Zero initialization

        res.m[0][0] = 1.0f / (aspect * tanHalfFov);
        res.m[1][1] = 1.0f / tanHalfFov;
        res.m[2][2] = zFar / (zNear - zFar);
        res.m[2][3] = -1.0f;
        res.m[3][2] = -(zFar * zNear) / (zFar - zNear);
        return res;
    }

    Mat4f Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 zNear, float32 zFar) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(left) && IsFinite(right) &&
                   IsFinite(bottom) && IsFinite(top) &&
                   IsFinite(zNear) && IsFinite(zFar));
        DNG_ASSERT(left != right && bottom != top && zNear != zFar &&
                   "Orthographic requires non-degenerate volume.");
#endif
        // RH, 0-1 Z range
        // Precondition: left != right, bottom != top, zNear != zFar
        Mat4f res = Mat4f::Identity();
        res.m[0][0] = 2.0f / (right - left);
        res.m[1][1] = 2.0f / (top - bottom);
        res.m[2][2] = 1.0f / (zNear - zFar);
        res.m[3][0] = -(right + left) / (right - left);
        res.m[3][1] = -(top + bottom) / (top - bottom);
        res.m[3][2] = zNear / (zNear - zFar);
        return res;
    }

    // ------------------------------------------------------------------------
    // Quaternion Operations
    // ------------------------------------------------------------------------

    Quatf FromAxisAngle(const Vec3f& axis, float32 angleRadians) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(axis.x) && IsFinite(axis.y) && IsFinite(axis.z));
        const float32 axisLengthSq = LengthSquared(axis);
        DNG_ASSERT(axisLengthSq > Epsilon * Epsilon && "FromAxisAngle called with near-zero axis.");
#endif
        const Vec3f unitAxis = Normalize(axis);
        float32 halfAngle = angleRadians * 0.5f;
        float32 s = Sin(halfAngle);
        return Quatf(unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, Cos(halfAngle));
    }

    Quatf FromEuler(float32 pitch, float32 yaw, float32 roll) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(pitch) && IsFinite(yaw) && IsFinite(roll));
#endif
        // Order: YXZ (Yaw, Pitch, Roll) - common in games
        float32 c1 = Cos(yaw / 2.0f);
        float32 c2 = Cos(pitch / 2.0f);
        float32 c3 = Cos(roll / 2.0f);
        float32 s1 = Sin(yaw / 2.0f);
        float32 s2 = Sin(pitch / 2.0f);
        float32 s3 = Sin(roll / 2.0f);

        return Quatf(
            s1 * c2 * s3 + c1 * s2 * c3,
            s1 * c2 * c3 - c1 * s2 * s3,
            c1 * c2 * s3 - s1 * s2 * c3,
            c1 * c2 * c3 + s1 * s2 * s3
        );
    }

    Mat4f ToMatrix(const Quatf& q) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(q.x) && IsFinite(q.y) && IsFinite(q.z) && IsFinite(q.w));
#endif
        Mat4f res = Mat4f::Identity();
        float32 xx = q.x * q.x;
        float32 yy = q.y * q.y;
        float32 zz = q.z * q.z;
        float32 xy = q.x * q.y;
        float32 xz = q.x * q.z;
        float32 yz = q.y * q.z;
        float32 wx = q.w * q.x;
        float32 wy = q.w * q.y;
        float32 wz = q.w * q.z;

        res.m[0][0] = 1.0f - 2.0f * (yy + zz);
        res.m[1][0] = 2.0f * (xy - wz);
        res.m[2][0] = 2.0f * (xz + wy);

        res.m[0][1] = 2.0f * (xy + wz);
        res.m[1][1] = 1.0f - 2.0f * (xx + zz);
        res.m[2][1] = 2.0f * (yz - wx);

        res.m[0][2] = 2.0f * (xz - wy);
        res.m[1][2] = 2.0f * (yz + wx);
        res.m[2][2] = 1.0f - 2.0f * (xx + yy);

        return res;
    }

    Quatf Slerp(const Quatf& a, const Quatf& b, float32 t) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(a.x) && IsFinite(a.y) && IsFinite(a.z) && IsFinite(a.w));
        DNG_ASSERT(IsFinite(b.x) && IsFinite(b.y) && IsFinite(b.z) && IsFinite(b.w));
        DNG_ASSERT(t >= 0.0f && t <= 1.0f && "Slerp requires 0 <= t <= 1.");
#endif
        float32 dot = Dot(a, b);
        Quatf r = b;
        if (dot < 0.0f)
        {
            dot = -dot;
            r = -r;
        }

        if (dot > 0.9995f)
        {
            // Linear interpolation for small angles
            return Normalize(a + (r - a) * t);
        }

        float32 theta_0 = Acos(dot);
        float32 theta = theta_0 * t;
        float32 sin_theta = Sin(theta);
        float32 sin_theta_0 = Sin(theta_0);

#if defined(DNG_ASSERT)
        DNG_ASSERT(Abs(sin_theta_0) > Epsilon && "Slerp encountered numerical instability (sin(theta_0) too small).");
#endif

        float32 s0 = Cos(theta) - dot * sin_theta / sin_theta_0;
        float32 s1 = sin_theta / sin_theta_0;

        return (a * s0) + (r * s1);
    }

} // namespace dng
