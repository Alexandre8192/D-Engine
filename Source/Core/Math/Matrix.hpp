#pragma once
// ============================================================================
// D-Engine - Source/Core/Math/Matrix.hpp
// ----------------------------------------------------------------------------
// Purpose : Concrete matrix types (Mat3f, Mat4f) and operations.
// Contract: Column-major storage.
//           Multiplication order: M * v (Vector on the right).
// Notes   : Float-first implementation. Heavy ops are out-of-line.
// ============================================================================

#include "Core/Math/Vector.hpp"
#include <cstring> // std::memcpy

namespace dng
{
    // ------------------------------------------------------------------------
    // Mat3f (3x3 Matrix)
    // ------------------------------------------------------------------------
    struct Mat3f
    {
        float32 m[3][3]; // [col][row]

        constexpr Mat3f() noexcept : m{ {0} } {}

        static constexpr Mat3f Identity() noexcept
        {
            Mat3f res;
            res.m[0][0] = 1.0f; res.m[1][1] = 1.0f; res.m[2][2] = 1.0f;
            return res;
        }

        static constexpr Mat3f Scale(float32 s) noexcept
        {
            Mat3f res;
            res.m[0][0] = s; res.m[1][1] = s; res.m[2][2] = s;
            return res;
        }

        static constexpr Mat3f Scale(const Vec3f& s) noexcept
        {
            Mat3f res;
            res.m[0][0] = s.x; res.m[1][1] = s.y; res.m[2][2] = s.z;
            return res;
        }
    };

    [[nodiscard]] constexpr Vec3f operator*(const Mat3f& m, const Vec3f& v) noexcept
    {
        return Vec3f(
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z
        );
    }

    [[nodiscard]] constexpr Mat3f operator*(const Mat3f& a, const Mat3f& b) noexcept
    {
        Mat3f res;
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r)
            {
                res.m[c][r] = a.m[0][r] * b.m[c][0] +
                              a.m[1][r] * b.m[c][1] +
                              a.m[2][r] * b.m[c][2];
            }
        }
        return res;
    }

    // ------------------------------------------------------------------------
    // Mat4f (4x4 Matrix)
    // ------------------------------------------------------------------------
    struct Mat4f
    {
        float32 m[4][4]; // [col][row]

        constexpr Mat4f() noexcept : m{ {0} } {}

        static constexpr Mat4f Identity() noexcept
        {
            Mat4f res;
            res.m[0][0] = 1.0f; res.m[1][1] = 1.0f; res.m[2][2] = 1.0f; res.m[3][3] = 1.0f;
            return res;
        }

        static constexpr Mat4f Translation(const Vec3f& t) noexcept
        {
            Mat4f res = Identity();
            res.m[3][0] = t.x;
            res.m[3][1] = t.y;
            res.m[3][2] = t.z;
            return res;
        }

        static constexpr Mat4f Scale(const Vec3f& s) noexcept
        {
            Mat4f res;
            res.m[0][0] = s.x;
            res.m[1][1] = s.y;
            res.m[2][2] = s.z;
            res.m[3][3] = 1.0f;
            return res;
        }
    };

    [[nodiscard]] constexpr Vec4f operator*(const Mat4f& m, const Vec4f& v) noexcept
    {
        return Vec4f(
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w,
            m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w
        );
    }

    [[nodiscard]] constexpr Mat4f operator*(const Mat4f& a, const Mat4f& b) noexcept
    {
        Mat4f res;
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                res.m[c][r] = a.m[0][r] * b.m[c][0] +
                              a.m[1][r] * b.m[c][1] +
                              a.m[2][r] * b.m[c][2] +
                              a.m[3][r] * b.m[c][3];
            }
        }
        return res;
    }

    // ------------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------------

    [[nodiscard]] constexpr Vec3f TransformPoint(const Mat4f& m, const Vec3f& p) noexcept
    {
        // Assumes w=1
        float32 x = m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0];
        float32 y = m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1];
        float32 z = m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2];
        float32 w = m.m[0][3] * p.x + m.m[1][3] * p.y + m.m[2][3] * p.z + m.m[3][3];

        if (Abs(w - 1.0f) > Epsilon && Abs(w) > Epsilon)
        {
            float32 invW = 1.0f / w;
            return Vec3f(x * invW, y * invW, z * invW);
        }
        return Vec3f(x, y, z);
    }

    [[nodiscard]] constexpr Vec3f TransformVector(const Mat4f& m, const Vec3f& v) noexcept
    {
        // Assumes w=0
        return Vec3f(
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z
        );
    }

    // ------------------------------------------------------------------------
    // Heavy Operations (Implemented in Math.cpp)
    // ------------------------------------------------------------------------

    // Returns Identity if matrix is singular (determinant ~ 0).
    [[nodiscard]] Mat4f Inverse(const Mat4f& m) noexcept;
    [[nodiscard]] Mat4f Transpose(const Mat4f& m) noexcept;
    
    // Precondition: 'up' must not be parallel to (target - eye).
    [[nodiscard]] Mat4f LookAt(const Vec3f& eye, const Vec3f& target, const Vec3f& up) noexcept;
    
    // Precondition: zNear != zFar.
    [[nodiscard]] Mat4f Perspective(float32 fovY, float32 aspect, float32 zNear, float32 zFar) noexcept;
    
    // Precondition: left != right, bottom != top, zNear != zFar.
    [[nodiscard]] Mat4f Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 zNear, float32 zFar) noexcept;

} // namespace dng
