#pragma once

// ============================================================================
// D-Engine - Source/Core/Simd/SimdVec4.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a thin, SIMD-friendly abstraction for 4-wide float vectors.
//           The initial implementation is a scalar fallback, but the API is
//           designed so platform-specific SIMD backends (SSE, AVX, NEON, etc.)
//           can be plugged in later without changing call sites.
// ----------------------------------------------------------------------------
// Contract:
//   - No dynamic allocations, no exceptions, no RTTI.
//   - Header must be self-contained: only standard headers included here.
//   - All operations are defined for 4-wide float vectors and are safe to call
//     with any bit pattern (no preconditions beyond "pointers must be valid").
//   - Alignment requirements are explicit: LoadAligned/StoreAligned require
//     the pointer to be aligned to at least 16 bytes when a SIMD backend is
//     enabled. The scalar fallback does not rely on alignment.
// ----------------------------------------------------------------------------
// Notes:
//   - This header intentionally uses "float" as the scalar type to remain
//     self-contained. If you prefer a custom typedef (float32), you can either
//     typedef it to float globally or adjust this header accordingly.
//   - The scalar fallback is deliberately simple and easy to read. When you
//     introduce a SIMD backend, you will typically:
//       * Keep the API surface identical.
//       * Replace the implementation bodies with intrinsics in a separate
//         backend-specific section or file.
//   - This is a low-level building block intended for hot code paths such as
//     matrix-vector multiplication, transform batches, and interpolation. Keep
//     the API tight and pay attention to inlining.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>

namespace dng
{
    namespace simd
    {
        // --------------------------------------------------------------------
        // Capability flags
        // --------------------------------------------------------------------
        // In the future, you can expose separate flags for SSE2, AVX2, NEON,
        // etc. For now, the scalar fallback reports "no dedicated SIMD backend".
        namespace detail
        {
            // This flag can be specialized per platform in a dedicated backend
            // header (for example SimdBackend_SSE2.hpp) if needed.
            static constexpr bool kHasSimdFloat4Backend = false;
        } // namespace detail

        [[nodiscard]] inline constexpr bool HasSimdFloat4() noexcept
        {
            return detail::kHasSimdFloat4Backend;
        }

        // --------------------------------------------------------------------
        // Float4
        // --------------------------------------------------------------------
        // Scalar representation of a 4-wide float vector. In a SIMD backend,
        // this struct will typically wrap a native SIMD register (for example
        // __m128 on SSE or float32x4_t on NEON).
        struct Float4
        {
            float x;
            float y;
            float z;
            float w;

            // Constructors are kept very simple. In a SIMD backend, you may
            // want to store the data in an array or in a native SIMD type and
            // provide equivalent constructors.
            constexpr Float4() noexcept
                : x(0.0f)
                , y(0.0f)
                , z(0.0f)
                , w(0.0f)
            {
            }

            constexpr Float4(float inX, float inY, float inZ, float inW) noexcept
                : x(inX)
                , y(inY)
                , z(inZ)
                , w(inW)
            {
            }
        };

        // --------------------------------------------------------------------
        // Construction helpers
        // --------------------------------------------------------------------

        [[nodiscard]] inline constexpr Float4 Zero() noexcept
        {
            return Float4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        [[nodiscard]] inline constexpr Float4 Set(float x, float y, float z, float w) noexcept
        {
            return Float4(x, y, z, w);
        }

        [[nodiscard]] inline constexpr Float4 Broadcast(float v) noexcept
        {
            return Float4(v, v, v, v);
        }

        // --------------------------------------------------------------------
        // Load / Store
        // --------------------------------------------------------------------
        // Contract:
        //   - ptr must be a valid pointer to at least 4 floats.
        //   - LoadAligned/StoreAligned require ptr to be 16-byte aligned in SIMD
        //     backends. The scalar fallback does not rely on alignment and is
        //     safe for any valid pointer.

        [[nodiscard]] inline Float4 Load(const float* ptr) noexcept
        {
            // Scalar fallback: simple load.
            return Float4(ptr[0], ptr[1], ptr[2], ptr[3]);
        }

        [[nodiscard]] inline Float4 LoadAligned(const float* ptr) noexcept
        {
            // Scalar fallback does not care about alignment. The name is kept
            // for API symmetry with future SIMD backends.
            return Load(ptr);
        }

        inline void Store(float* ptr, const Float4& v) noexcept
        {
            ptr[0] = v.x;
            ptr[1] = v.y;
            ptr[2] = v.z;
            ptr[3] = v.w;
        }

        inline void StoreAligned(float* ptr, const Float4& v) noexcept
        {
            // Scalar fallback: same as Store. SIMD backends may use aligned
            // store instructions here.
            Store(ptr, v);
        }

        // --------------------------------------------------------------------
        // Basic arithmetic
        // --------------------------------------------------------------------

        [[nodiscard]] inline constexpr Float4 Add(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                a.x + b.x,
                a.y + b.y,
                a.z + b.z,
                a.w + b.w
            );
        }

        [[nodiscard]] inline constexpr Float4 Sub(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                a.x - b.x,
                a.y - b.y,
                a.z - b.z,
                a.w - b.w
            );
        }

        [[nodiscard]] inline constexpr Float4 Mul(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                a.x * b.x,
                a.y * b.y,
                a.z * b.z,
                a.w * b.w
            );
        }

        [[nodiscard]] inline constexpr Float4 Mul(const Float4& a, float scalar) noexcept
        {
            return Float4(
                a.x * scalar,
                a.y * scalar,
                a.z * scalar,
                a.w * scalar
            );
        }

        [[nodiscard]] inline constexpr Float4 Mul(float scalar, const Float4& a) noexcept
        {
            return Mul(a, scalar);
        }

        [[nodiscard]] inline constexpr Float4 Negate(const Float4& a) noexcept
        {
            return Float4(
                -a.x,
                -a.y,
                -a.z,
                -a.w
            );
        }

        // Fused multiply-add: a * b + c.
        // In a SIMD backend, this can map to a single FMA instruction when
        // available, or to Mul + Add otherwise.
        [[nodiscard]] inline constexpr Float4 Fmadd(const Float4& a, const Float4& b, const Float4& c) noexcept
        {
            return Add(Mul(a, b), c);
        }

        // --------------------------------------------------------------------
        // Min / Max
        // --------------------------------------------------------------------

        [[nodiscard]] inline constexpr Float4 Min(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (a.x < b.x) ? a.x : b.x,
                (a.y < b.y) ? a.y : b.y,
                (a.z < b.z) ? a.z : b.z,
                (a.w < b.w) ? a.w : b.w
            );
        }

        [[nodiscard]] inline constexpr Float4 Max(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (a.x > b.x) ? a.x : b.x,
                (a.y > b.y) ? a.y : b.y,
                (a.z > b.z) ? a.z : b.z,
                (a.w > b.w) ? a.w : b.w
            );
        }

        // --------------------------------------------------------------------
        // Dot product and helpers
        // --------------------------------------------------------------------

        [[nodiscard]] inline constexpr float Dot(const Float4& a, const Float4& b) noexcept
        {
            return a.x * b.x
                 + a.y * b.y
                 + a.z * b.z
                 + a.w * b.w;
        }

        [[nodiscard]] inline float Length(const Float4& v) noexcept
        {
            return std::sqrt(Dot(v, v));
        }

        [[nodiscard]] inline Float4 Normalize(const Float4& v, float epsilon = 1e-8f) noexcept
        {
            float lenSq = Dot(v, v);
            if (lenSq <= epsilon)
            {
                return Zero();
            }

            float invLen = 1.0f / std::sqrt(lenSq);
            return Mul(v, invLen);
        }

        // --------------------------------------------------------------------
        // Component-wise comparisons (masks)
        // --------------------------------------------------------------------
        // For now, masks are returned as Float4 with 0.0f or 1.0f components.
        // In a SIMD backend, you may choose to represent masks as native
        // integer vectors and expose dedicated APIs if needed.

        [[nodiscard]] inline constexpr Float4 CompareEqual(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (a.x == b.x) ? 1.0f : 0.0f,
                (a.y == b.y) ? 1.0f : 0.0f,
                (a.z == b.z) ? 1.0f : 0.0f,
                (a.w == b.w) ? 1.0f : 0.0f
            );
        }

        [[nodiscard]] inline constexpr Float4 CompareLess(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (a.x < b.x) ? 1.0f : 0.0f,
                (a.y < b.y) ? 1.0f : 0.0f,
                (a.z < b.z) ? 1.0f : 0.0f,
                (a.w < b.w) ? 1.0f : 0.0f
            );
        }

        [[nodiscard]] inline constexpr Float4 CompareGreater(const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (a.x > b.x) ? 1.0f : 0.0f,
                (a.y > b.y) ? 1.0f : 0.0f,
                (a.z > b.z) ? 1.0f : 0.0f,
                (a.w > b.w) ? 1.0f : 0.0f
            );
        }

        // --------------------------------------------------------------------
        // Select (blend)
        // --------------------------------------------------------------------
        // Select between two vectors based on a mask. The mask is interpreted
        // as 0.0f = false, non-zero = true per component.
        [[nodiscard]] inline constexpr Float4 Select(const Float4& mask, const Float4& a, const Float4& b) noexcept
        {
            return Float4(
                (mask.x != 0.0f) ? a.x : b.x,
                (mask.y != 0.0f) ? a.y : b.y,
                (mask.z != 0.0f) ? a.z : b.z,
                (mask.w != 0.0f) ? a.w : b.w
            );
        }

    } // namespace simd
} // namespace dng
