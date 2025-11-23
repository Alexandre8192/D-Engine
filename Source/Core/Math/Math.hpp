#pragma once
// ============================================================================
// D-Engine - Source/Core/Math/Math.hpp
// ----------------------------------------------------------------------------
// Purpose : Scalar math functions, constants, and common utilities.
// Contract: All functions are `constexpr` and `noexcept` where possible.
//           Angles are in Radians by default.
// Notes   : Uses `float32` as the primary scalar type.
// ============================================================================

#include "Core/CoreMinimal.hpp"
#include <cmath>
#include <limits>
#include <algorithm>
#include <numbers>

namespace dng
{
    // ------------------------------------------------------------------------
    // Constants
    // ------------------------------------------------------------------------

    constexpr float32 Pi = std::numbers::pi_v<float32>;
    constexpr float32 TwoPi = 2.0f * Pi;
    constexpr float32 HalfPi = 0.5f * Pi;
    constexpr float32 Epsilon = std::numeric_limits<float32>::epsilon();
    constexpr float32 HugeValue = std::numeric_limits<float32>::max();



    // ------------------------------------------------------------------------
    // Scalar Helpers
    // ------------------------------------------------------------------------

    [[nodiscard]] constexpr float32 Radians(float32 degrees) noexcept
    {
        return degrees * (Pi / 180.0f);
    }

    [[nodiscard]] constexpr float32 Degrees(float32 radians) noexcept
    {
        return radians * (180.0f / Pi);
    }

    template <typename T>
    [[nodiscard]] constexpr T Clamp(T value, T min, T max) noexcept
    {
        return (value < min) ? min : (value > max) ? max : value;
    }

    namespace detail
    {
        // Default Lerp policy: works for scalar types and POD-like structs
        // that support +, -, and scalar multiplication by float32.
        template <typename T>
        struct LerpPolicy
        {
            static constexpr bool kHasCustomLerp = false;

            [[nodiscard]] static constexpr T Lerp(const T& a, const T& b, float32 t) noexcept
            {
                return a + (b - a) * t;
            }
        };
    } // namespace detail



    template <typename T>
    [[nodiscard]] constexpr T Lerp(T a, T b, float32 t) noexcept
    {
        // We intentionally take a and b by value so callers can pass temporaries or literals.
        // The policy can still treat them as const references internally.
        return detail::LerpPolicy<T>::Lerp(a, b, t);
    }

    template <typename T>
    [[nodiscard]] constexpr T Min(T a, T b) noexcept
    {
        return (a < b) ? a : b;
    }

    template <typename T>
    [[nodiscard]] constexpr T Max(T a, T b) noexcept
    {
        return (a > b) ? a : b;
    }

    template <typename T>
    [[nodiscard]] constexpr T Abs(T a) noexcept
    {
        return (a < T(0)) ? -a : a;
    }

    template <typename T>
    [[nodiscard]] constexpr T Sign(T a) noexcept
    {
        return (a < T(0)) ? T(-1) : (a > T(0)) ? T(1) : T(0);
    }

    [[nodiscard]] constexpr float32 Saturate(float32 value) noexcept
    {
        return Clamp(value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline bool IsNearlyEqual(float32 a, float32 b, float32 epsilon = Epsilon) noexcept
    {
        return Abs(a - b) <= epsilon;
    }

    [[nodiscard]] inline bool IsFinite(float32 value) noexcept
    {
        return std::isfinite(value);
    }

    [[nodiscard]] inline float32 Sqrt(float32 value) noexcept
    {
        return std::sqrt(value);
    }

    [[nodiscard]] inline float32 Tan(float32 radians) noexcept
    {
        return std::tan(radians);
    }

    [[nodiscard]] inline float32 Cos(float32 radians) noexcept
    {
        return std::cos(radians);
    }

    [[nodiscard]] inline float32 Sin(float32 radians) noexcept
    {
        return std::sin(radians);
    }

    [[nodiscard]] inline float32 Acos(float32 value) noexcept
    {
        return std::acos(value);
    }

    [[nodiscard]] inline float32 Atan2(float32 y, float32 x) noexcept
    {
        return std::atan2(y, x);
    }

        // ------------------------------------------------------------------------
    // Scalar wrapping and snapping helpers
    // ------------------------------------------------------------------------

    // Simple modulo helper for float32.
    [[nodiscard]] inline float32 Mod(float32 value, float32 divisor) noexcept
    {
        if (divisor == 0.0f)
        {
            return value;
        }

        float32 result = std::fmod(value, divisor);
        // Ensure the result has the same sign as the divisor.
        if ((result < 0.0f && divisor > 0.0f) || (result > 0.0f && divisor < 0.0f))
        {
            result += divisor;
        }
        return result;
    }

    // Wrap value inside [min, max) using modulo arithmetic.
    [[nodiscard]] inline float32 Wrap(float32 value, float32 min, float32 max) noexcept
    {
        const float32 range = max - min;
        if (range <= 0.0f)
        {
            return value;
        }

        float32 wrapped = std::fmod(value - min, range);
        if (wrapped < 0.0f)
        {
            wrapped += range;
        }
        return wrapped + min;
    }

    // Convenience helper to wrap an angle in radians into [-Pi, Pi).
    [[nodiscard]] inline float32 WrapAngle(float32 radians) noexcept
    {
        return Wrap(radians, -Pi, Pi);
    }

    // Snap value to the nearest multiple of gridSize.
    [[nodiscard]] inline float32 GridSnap(float32 value, float32 gridSize) noexcept
    {
        if (gridSize <= 0.0f)
        {
            return value;
        }

        const float32 invGrid = 1.0f / gridSize;
        return std::round(value * invGrid) * gridSize;
    }

    template <typename T>
    [[nodiscard]] inline T GridSnap(T value, T gridSize) noexcept
    {
        if (gridSize <= T(0))
        {
            return value;
        }

        const double invGrid = 1.0 / static_cast<double>(gridSize);
        const double snapped = std::round(static_cast<double>(value) * invGrid) * static_cast<double>(gridSize);
        return static_cast<T>(snapped);
    }

    // Note: GridSnap is not constexpr because it uses std::round.

    // ------------------------------------------------------------------------
    // Diagnostics helpers (no-throw, debug-only assertions)
    // ------------------------------------------------------------------------

    [[nodiscard]] inline bool IsNearlyZero(float32 value, float32 epsilon = Epsilon) noexcept
    {
        return Abs(value) <= epsilon;
    }

    [[nodiscard]] inline bool IsUnitLength(float32 lengthSquared, float32 tolerance = 1e-3f) noexcept
    {
        // This is meant to be used with "lengthSquared" to avoid an extra sqrt.
        return IsNearlyEqual(lengthSquared, 1.0f, tolerance);
    }

    // Optional debug helpers: they are no-op in builds where DNG_ASSERT is compiled out.
    inline void AssertFinite(float32 value) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(value) && "Non-finite float32 detected.");
#else
        (void)value;
#endif
    }

    inline void AssertUnitLength(float32 lengthSquared, float32 tolerance = 1e-3f) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsUnitLength(lengthSquared, tolerance) && "Expected normalized value (length ~= 1).");
#else
        (void)lengthSquared;
        (void)tolerance;
#endif
    }

} // namespace dng
