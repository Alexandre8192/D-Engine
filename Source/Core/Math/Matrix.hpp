#pragma once
// ============================================================================
// D-Engine - Source/Core/Math/Matrix.hpp
// ----------------------------------------------------------------------------
// Purpose : Concrete matrix types (Mat3f, Mat4f) and operations.
// Contract: Column-major storage addressed via m[column][row]; vectors are treated as column vectors multiplied on the right.
// Notes   : Float-first implementation. Heavy ops are out-of-line.
// ============================================================================

#include "Core/Math/Vector.hpp"
#include <type_traits>

namespace dng
{
    // ------------------------------------------------------------------------
    // Mat3f (3x3 Matrix)
    // ------------------------------------------------------------------------
    // ---
    // Purpose : Compact 3x3 float matrix for linear (non-homogeneous) transforms.
    // Contract: Stored column-major via `m[col][row]`; trivially copyable; default ctor zero-inits for safety.
    // Notes   : Heavy operations (inverse/determinant) live in Math.cpp to keep header-only cost low.
    // ---
    struct Mat3f
    {
        float32 m[3][3]; // [col][row]

        constexpr Mat3f() noexcept : m{ {0} } {}

        // ---
        // Purpose : Build identity or scale matrices inline.
        // Contract: constexpr/noexcept; no hidden normalization; column ordering follows column-major contract.
        // Notes   : Use Mat4f variants for affine transforms requiring translation.
        // ---
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

    // ---
    // Purpose : Apply Mat3f to column-vectors (Vec3f) on the right.
    // Contract: Assumes column-vector convention for transforms; no perspective divide.
    // Notes   : Keeps deterministic cost (9 muls + 6 adds) inline for hot loops.
    // ---
    [[nodiscard]] constexpr Vec3f operator*(const Mat3f& m, const Vec3f& v) noexcept
    {
        return Vec3f(
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z
        );
    }

    // ---
    // Purpose : Compose two 3x3 matrices using column-major semantics.
    // Contract: Deterministic triple-loop (no allocations); safe for constexpr use.
    // Notes   : Outer loop iterates over destination columns to improve cache friendliness.
    // ---
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
    // ---
    // Purpose : 4x4 float matrix for homogeneous transforms across renderer/geometry subsystems.
    // Contract: Column-major storage accessed via `m[col][row]`; vectors are column vectors multiplied on the right (v' = M * v);
    //           trivially copyable; deterministic operations (no heap).
    // Notes   : Conventions are enforced by Math.cpp's Transpose/TransformPoint/LookAt/Perspective helpers and place translation in column 3.
    // ---
    struct Mat4f
    {
        float32 m[4][4]; // [col][row]

        constexpr Mat4f() noexcept : m{ {0} } {}

        // ---
        // Purpose : Inline constructors for canonical transforms (identity, translation, scale).
        // Contract: constexpr/noexcept; translation assumes column-vector * matrix ordering (translation stored in column 3).
        // Notes   : `Translation` writes into the last column to match column-major contract.
        // ---
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

    // ---
    // Purpose : Multiply Mat4f with Vec4f under column-vector semantics.
    // Contract: Deterministic set of 16 FMA-equivalent operations; no perspective divide baked in.
    // Notes   : Aligns with TransformPoint/TransformVector helpers for consistent behavior.
    // ---
    [[nodiscard]] constexpr Vec4f operator*(const Mat4f& m, const Vec4f& v) noexcept
    {
        return Vec4f(
            m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w,
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w,
            m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w
        );
    }

    // ---
    // Purpose : Compose affine transforms (column-major).
    // Contract: Deterministic 4x4 triple loop; no hidden allocations; safe for constexpr.
    // Notes   : Equivalent to composing column-major transforms while treating Vec4f inputs as column vectors.
    // ---
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

    // ---
    // Purpose : Transform a point (implicit w=1) using Mat4f under column-vector convention.
    // Contract: Performs perspective divide when w deviates from 1; tolerates near-singular w via epsilon guard.
    // Notes   : Translation lives in `m[3][0..2]` given column-major layout.
    // ---
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

    // ---
    // Purpose : Transform a direction vector (w=0) without translation.
    // Contract: Deterministic multiply; assumes column-vector semantics; no normalization performed.
    // Notes   : Use Normalize afterward when unit vectors are required.
    // ---
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
    // ---
    // Purpose : Compute the inverse of a general 4x4 column-major matrix.
    // Contract: Returns Identity when matrix is singular; noexcept; asserts on non-finite inputs when enabled.
    // Notes   : Suitable for affine transforms; cost ~200 FLOPs.
    // ---
    [[nodiscard]] Mat4f Inverse(const Mat4f& m) noexcept;

    // ---
    // Purpose : Transpose helper for Mat4f.
    // Contract: Pure register shuffle; noexcept; preserves column-major semantics.
    // Notes   : Provided inline in Math.cpp to keep header lean.
    // ---
    [[nodiscard]] Mat4f Transpose(const Mat4f& m) noexcept;
    
    // ---
    // Purpose : Build a right-handed view matrix.
    // Contract: `up` must not be parallel to (target - eye); returns column-major matrix expecting column-vector inputs.
    // Notes   : Depth range [0,1]; translation occupies column 3.
    // ---
    [[nodiscard]] Mat4f LookAt(const Vec3f& eye, const Vec3f& target, const Vec3f& up) noexcept;
    
    // ---
    // Purpose : Perspective projection builder (right-handed, depth in [0,1]).
    // Contract: Requires `0 < fovY < Pi`, `aspect > 0`, and `0 < zNear < zFar`; returns column-major matrix.
    // Notes   : Matches D3D-style clip range with -w in row 2 column 3.
    // ---
    [[nodiscard]] Mat4f Perspective(float32 fovY, float32 aspect, float32 zNear, float32 zFar) noexcept;
    
    // ---
    // Purpose : Orthographic projection builder (right-handed, depth in [0,1]).
    // Contract: Requires non-degenerate volume: left != right, bottom != top, zNear != zFar.
    // Notes   : Translation stored in column 3 similar to Perspective.
    // ---
    [[nodiscard]] Mat4f Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 zNear, float32 zFar) noexcept;

} // namespace dng

static_assert(std::is_trivially_copyable_v<dng::Mat4f>, "Mat4f must stay POD for SIMD interop");
static_assert(sizeof(dng::Mat4f) == 16u * sizeof(dng::float32), "Mat4f layout drifted from 16 floats");
