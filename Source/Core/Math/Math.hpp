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

    // ---
    // Purpose : Canonical math constants exposed as float32 for engine-wide consistency.
    // Contract: Values are constexpr, finite, and reflect IEEE-754 single-precision magnitudes.
    // Notes   : `Pi`/`TwoPi`/`HalfPi` drive all trig helpers; `Epsilon` + `HugeValue` gate tolerance checks.
    // ---
    constexpr float32 Pi = std::numbers::pi_v<float32>;
    constexpr float32 TwoPi = 2.0f * Pi;
    constexpr float32 HalfPi = 0.5f * Pi;
    constexpr float32 Epsilon = std::numeric_limits<float32>::epsilon();
    constexpr float32 HugeValue = std::numeric_limits<float32>::max();



    // ------------------------------------------------------------------------
    // Scalar Helpers
    // ------------------------------------------------------------------------

    // ---
    // Purpose : Convert angles between degree and radian domains.
    // Contract: Accepts any finite float32; constexpr/noexcept; no hidden allocations.
    // Notes   : Keeps conversions inline for call sites lacking <numbers>.
    // ---
    [[nodiscard]] constexpr float32 Radians(float32 degrees) noexcept
    {
        return degrees * (Pi / 180.0f);
    }

    [[nodiscard]] constexpr float32 Degrees(float32 radians) noexcept
    {
        return radians * (180.0f / Pi);
    }

    template <typename T>
    // ---
    // Purpose : Clamp a scalar to a closed interval.
    // Contract: Works for arithmetic types implementing `<` and copy semantics; branch-only, constexpr.
    // Notes   : Prefers inclusive bounds `[min, max]`; call sites must supply ordered limits.
    // ---
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

            // ---
            // Purpose : Provide a fallback linear interpolation for POD-like types.
            // Contract: Requires +, -, and scalar multiply overloads; pure constexpr, no heap interaction.
            // Notes   : Specialize this struct for types needing custom interpolation (e.g., Quatf → Slerp).
            // ---
            [[nodiscard]] static constexpr T Lerp(const T& a, const T& b, float32 t) noexcept
            {
                return a + (b - a) * t;
            }
        };
    } // namespace detail



    template <typename T>
    // ---
    // Purpose : Linearly interpolate between values a and b.
    // Contract: Accepts POD-like types that satisfy addition/subtraction and scalar multiply; `t` expected in [0,1].
    // Notes   : Takes inputs by value so temporaries/literals are cheap; specializations hook via LerpPolicy.
    // ---
    [[nodiscard]] constexpr T Lerp(T a, T b, float32 t) noexcept
    {
        // We intentionally take a and b by value so callers can pass temporaries or literals.
        // The policy can still treat them as const references internally.
        return detail::LerpPolicy<T>::Lerp(a, b, t);
    }

    template <typename T>
    // ---
    // Purpose : Compute the minimum of two scalars.
    // Contract: Requires `<` ordering and copyable operands; constexpr/noexcept.
    // Notes   : Branch-only; ties prefer the first argument.
    // ---
    [[nodiscard]] constexpr T Min(T a, T b) noexcept
    {
        return (a < b) ? a : b;
    }

    template <typename T>
    // ---
    // Purpose : Compute the maximum of two scalars.
    // Contract: Requires `<` ordering and copyable operands; constexpr/noexcept.
    // Notes   : Branch-only; ties prefer the first argument.
    // ---
    [[nodiscard]] constexpr T Max(T a, T b) noexcept
    {
        return (a > b) ? a : b;
    }

    template <typename T>
    // ---
    // Purpose : Absolute value helper for signed arithmetic types.
    // Contract: Requires unary minus; constexpr/noexcept; no overflow guard beyond caller responsibility.
    // Notes   : Mirrors std::abs but constexpr and header-only.
    // ---
    [[nodiscard]] constexpr T Abs(T a) noexcept
    {
        return (a < T(0)) ? -a : a;
    }

    template <typename T>
    // ---
    // Purpose : Return the sign of a scalar (-1, 0, +1).
    // Contract: Requires comparisons to zero and copy semantics; constexpr/noexcept.
    // Notes   : Useful for branchless orientation checks.
    // ---
    [[nodiscard]] constexpr T Sign(T a) noexcept
    {
        return (a < T(0)) ? T(-1) : (a > T(0)) ? T(1) : T(0);
    }

    // ---
    // Purpose : Clamp floats into the normalized [0,1] interval.
    // Contract: constexpr/noexcept; no side effects; uses Clamp helper.
    // Notes   : Preferred for color/alpha saturations.
    // ---
    [[nodiscard]] constexpr float32 Saturate(float32 value) noexcept
    {
        return Clamp(value, 0.0f, 1.0f);
    }

    // ---
    // Purpose : Compare two floats with a configurable absolute tolerance.
    // Contract: Default epsilon equals machine epsilon; noexcept; avoids NaN checks (caller responsibility).
    // Notes   : Use for deterministic tests rather than relying on ==.
    // ---
    [[nodiscard]] inline bool IsNearlyEqual(float32 a, float32 b, float32 epsilon = Epsilon) noexcept
    {
        return Abs(a - b) <= epsilon;
    }

    // ---
    // Purpose : Validate that a float is finite (not NaN/Inf).
    // Contract: Thin wrapper around std::isfinite; noexcept.
    // Notes   : Used by diagnostics before trusting inputs.
    // ---
    [[nodiscard]] inline bool IsFinite(float32 value) noexcept
    {
        return std::isfinite(value);
    }

    // ---
    // Purpose : Inline sqrt/trig wrappers keeping STL exposure centralized.
    // Contract: Accepts finite floats; delegates to <cmath>; noexcept per platform intrinsics.
    // Notes   : Provide consistent naming with other math helpers.
    // ---
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
    // ---
    // Purpose : Provide deterministic wrapping helpers for cyclic domains.
    // Contract: Handle zero divisors by passthrough; rely on std::fmod; noexcept.
    // Notes   : Sign of the result matches divisor to avoid branchy correction at call site.
    // ---
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
    // ---
    // Purpose : Wrap a value into a semi-open interval [min, max).
    // Contract: Requires `max > min`; noexcept except for std::fmod; preserves determinism.
    // Notes   : Useful for angles and cyclic parameters.
    // ---
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
    // ---
    // Purpose : Wrap radians into [-Pi, Pi).
    // Contract: Relies on Wrap helper; noexcept except std::fmod.
    // Notes   : Maintains RH convention expectations for yaw/pitch/roll.
    // ---
    [[nodiscard]] inline float32 WrapAngle(float32 radians) noexcept
    {
        return Wrap(radians, -Pi, Pi);
    }

    // Snap value to the nearest multiple of gridSize.
    // ---
    // Purpose : Snap scalars to a discrete grid.
    // Contract: `gridSize > 0`; uses std::round; noexcept.
    // Notes   : Float overload avoids precision loss by precomputing inverse grid.
    // ---
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
    // ---
    // Purpose : Integral-friendly snap helper.
    // Contract: Works for integral/floating-point T convertible to double; gridSize > 0 enforced.
    // Notes   : Uses double intermediates to minimize overflow; not constexpr due to std::round.
    // ---
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

    // ---
    // Purpose : Diagnostics helpers mirroring engine Float utilities.
    // Contract: No side effects; constexpr where possible; intended for debug assertions.
    // Notes   : Provide consistent tolerance semantics across math subsystems.
    // ---
    [[nodiscard]] inline bool IsNearlyZero(float32 value, float32 epsilon = Epsilon) noexcept
    {
        return Abs(value) <= epsilon;
    }

    // ---
    // Purpose : Check if a squared length is approximately one within tolerance.
    // Contract: Avoids extra sqrt by operating on squared magnitudes; tolerance defaults to 1e-3.
    // Notes   : Use with LengthSquared outputs to keep hot paths branchless.
    // ---
    [[nodiscard]] inline bool IsUnitLength(float32 lengthSquared, float32 tolerance = 1e-3f) noexcept
    {
        // This is meant to be used with "lengthSquared" to avoid an extra sqrt.
        return IsNearlyEqual(lengthSquared, 1.0f, tolerance);
    }

    // Optional debug helpers: they are no-op in builds where DNG_ASSERT is compiled out.
    // ---
    // Purpose : Debug-time guard ensuring a float stays finite before use.
    // Contract: Enabled only when DNG_ASSERT is defined; otherwise compiles to no-op.
    // Notes   : Keeps diagnostics lightweight without forcing call sites to branch.
    // ---
    inline void AssertFinite(float32 value) noexcept
    {
#if defined(DNG_ASSERT)
        DNG_ASSERT(IsFinite(value) && "Non-finite float32 detected.");
#else
        (void)value;
#endif
    }

    // ---
    // Purpose : Validate that a precomputed length-squared is approximately one.
    // Contract: Tolerance defaults to 1e-3; debug-only assertion; no hidden work in release builds.
    // Notes   : Keeps normalization assumptions explicit without extra square roots.
    // ---
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
