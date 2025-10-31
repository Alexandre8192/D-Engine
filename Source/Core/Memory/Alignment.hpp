#pragma once
// ============================================================================
// D-Engine - Core/Memory/Alignment.hpp
// ----------------------------------------------------------------------------
// Purpose : Centralize all alignment math (predicate helpers, normalization,
//           and pointer/integer adjustment) in constexpr-friendly utilities.
// Contract: Every allocator and caller must route alignment math through these
//           helpers to guarantee power-of-two results >= alignof(max_align_t).
// Notes   : Integer overloads are constrained to unsigned types; pointer
//           variants emit logger diagnostics when adjustments occur. Saturation
//           avoids UB on extreme inputs to keep behavior deterministic.
// ============================================================================

#include <cstddef>      // std::size_t, std::max_align_t
#include <cstdint>      // std::uintptr_t
#include <limits>       // std::numeric_limits
#include <type_traits>  // std::is_integral_v, std::is_unsigned_v

#include "Core/Diagnostics/Check.hpp"
#include "Core/Logger.hpp" // DNG_LOG_*

#ifndef DNG_LOGCAT_ALIGNMENT
#define DNG_LOGCAT_ALIGNMENT "Memory.Alignment"
#endif

namespace dng::core
{
    // Prefer an unsigned integral for sizes/alignments.
    using usize = std::size_t;

    // =========================================================================
    // Basic predicate: IsPowerOfTwo
    // =========================================================================
    // ---
    // Purpose : Test whether the provided unsigned value has exactly one bit set.
    // Contract: Accepts any `usize`; returns true only when value > 0 and power-of-two.
    // Notes   : constexpr-friendly so callers can guard static_assert invariants.
    // ---
    [[nodiscard]] constexpr bool IsPowerOfTwo(usize x) noexcept
    {
        // For x > 0: power-of-two if only a single bit is set.
        return (x != 0) && ((x & (x - 1)) == 0);
    }

    // =========================================================================
    // detail: highest/next power-of-two helpers with saturation
    // =========================================================================
    namespace detail
    {
        // ---
        // Purpose : Compute the largest power-of-two value representable in `usize`.
        // Contract: constexpr, no side effects, valid for all platforms.
        // Notes   : Used to saturate alignment computations when inputs overflow.
        // ---
        [[nodiscard]] constexpr usize HighestPow2() noexcept
        {
            // `digits` = number of value bits (no sign bit for unsigned types).
            // Example: size_t on 64-bit -> 64, we need 1 << (64-1) = 1<<63.
            return (usize{ 1 } << (std::numeric_limits<usize>::digits - 1));
        }

        // ---
        // Purpose : Round arbitrary unsigned input up to the next power-of-two with saturation.
        // Contract: Accepts any `usize`; returns at least 1; clamps to HighestPow2() on overflow.
        // Notes   : Keeps alignment math deterministic even for adversarial SIZE_MAX requests.
        // ---
        [[nodiscard]] constexpr usize NextPow2Saturated(usize x) noexcept
        {
            if (x == 0) return usize{ 1 };
            if (IsPowerOfTwo(x)) return x;

            // Compute highest set bit position (0-based).
            // Example: x in (2^k, 2^(k+1)) -> next power is 1 << (k+1).
            const int totalBits = std::numeric_limits<usize>::digits;
            int msb = 0;
            {
                usize tmp = x;
                // Count leading zeros manually (portable).
                // We move until tmp == 0, tracking index of most significant bit.
                for (int i = 0; i < totalBits; ++i)
                {
                    if (tmp == 0) break;
                    tmp >>= 1;
                    msb = i; // index of last shift before tmp becomes 0
                }
            }

            // Next power-of-two would be (msb+1).
            // If msb == totalBits-1 then x is already >= highest pow2,
            // so the "next" would overflow -> saturate.
            if (msb >= totalBits - 1)
            {
                // No logging here (constexpr). The pointer overloads will log when applicable.
                return HighestPow2();
            }

            const int nextBit = msb + 1;
            return (usize{ 1 } << nextBit);
        }

        // ---
        // Purpose : Detect unsigned addition overflow for helper routines.
        // Contract: Only accepts unsigned integral types; constexpr-safe.
        // Notes   : Centralises overflow detection to keep AlignUp implementation minimal.
        // ---
        template <class U>
        [[nodiscard]] constexpr bool add_would_overflow(U a, U b) noexcept
        {
            static_assert(std::is_unsigned_v<U>, "Overflow helper expects unsigned type");
            return a > (std::numeric_limits<U>::max)() - b;
        }
    } // namespace detail

    // =========================================================================
    // NormalizeAlignment
    // =========================================================================
    // ---
    // Purpose : Canonicalize caller-provided alignment to the engine's power-of-two policy.
    // Contract: Accepts any `usize`; maps zero to `alignof(std::max_align_t)`; never returns 0.
    // Notes   : Saturates on overflow so allocators never emit undefined behaviour.
    // ---
    [[nodiscard]] constexpr usize NormalizeAlignment(usize alignment) noexcept
    {
        const usize minAlign = alignof(std::max_align_t);

        // 0 means "default" -> promote to at least max_align_t
        if (alignment == 0)
            return minAlign;

        // Round up to next power-of-two with saturation, then enforce minAlign.
        const usize rounded = detail::NextPow2Saturated(alignment);
        return (rounded < minAlign) ? minAlign : rounded;
    }

    // =========================================================================
    // AlignUp / AlignDown for UNSIGNED integral types (overflow-safe)
    // =========================================================================
    // IMPORTANT:
    //   We constrain these templates to UNSIGNED integral types to avoid
    //   surprising results with negative values. If you have a signed value,
    //   cast it to an unsigned type (e.g., `usize`) first, then call AlignUp.
    // -------------------------------------------------------------------------

    // ---
    // Purpose : Bump an unsigned integral value up to the next aligned multiple.
    // Contract: T must be unsigned integral; alignment normalized via NormalizeAlignment().
    // Notes   : Clamps on overflow in debug builds to preserve deterministic behaviour.
    // ---
    template <class T>
    [[nodiscard]] constexpr T AlignUp(T value, usize alignment) noexcept
    {
        static_assert(std::is_integral_v<T>, "AlignUp<T>: T must be integral");
        static_assert(std::is_unsigned_v<T>, "AlignUp<T>: T must be UNSIGNED");
        const T a = static_cast<T>(NormalizeAlignment(alignment));
        const T mask = static_cast<T>(a - T{ 1 });

        // Already aligned?
        if ((value & mask) == T{ 0 })
            return value;

        // Overflow check before value + mask
        if (detail::add_would_overflow<T>(value, mask))
        {
            // Keep assert (works in non-constexpr evaluation); do not use logger here.
            DNG_ASSERT(false && "AlignUp overflow: value + (alignment-1) exceeds max");
            return (std::numeric_limits<T>::max)(); // clamp (defined, not UB)
        }

        const T aligned = (value + mask) & ~mask;
        return aligned;
    }

    // ---
    // Purpose : Truncate an unsigned integral value down to the previous aligned multiple.
    // Contract: T must be unsigned integral; alignment normalized before use.
    // Notes   : Pure arithmetic, no overflow paths.
    // ---
    template <class T>
    [[nodiscard]] constexpr T AlignDown(T value, usize alignment) noexcept
    {
        static_assert(std::is_integral_v<T>, "AlignDown<T>: T must be integral");
        static_assert(std::is_unsigned_v<T>, "AlignDown<T>: T must be UNSIGNED");
        const T a = static_cast<T>(NormalizeAlignment(alignment));
        const T mask = static_cast<T>(a - T{ 1 });
        return (value & ~mask);
    }

    // =========================================================================
    // Pointer overloads (use uintptr_t for arithmetic)
    // =========================================================================
    // ---
    // Purpose : Promote a raw pointer to the next alignment boundary.
    // Contract: Zero alignment treated as default; returns original pointer when already aligned.
    // Notes   : Emits guarded diagnostics to highlight unexpected realignment work.
    // ---
    [[nodiscard]] inline void* AlignUp(void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const std::uintptr_t aligned = static_cast<std::uintptr_t>(
            AlignUp<std::uintptr_t>(p, alignment));
        if (aligned != p)
        {
            if (Logger::IsEnabled(LogLevel::Info, DNG_LOGCAT_ALIGNMENT))
            {
                DNG_LOG_INFO(DNG_LOGCAT_ALIGNMENT, "AlignUp adjusted pointer {} -> {} (align={})", (void*)p, (void*)aligned, (size_t)NormalizeAlignment(alignment));
            }
        }
        return reinterpret_cast<void*>(aligned);
    }

    // ---
    // Purpose : Snap a raw pointer down to the previous alignment boundary.
    // Contract: Accepts null pointers; treats zero alignment as default.
    // Notes   : Logging remains conditional to avoid perturbing hot paths.
    // ---
    [[nodiscard]] inline void* AlignDown(void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const std::uintptr_t aligned = static_cast<std::uintptr_t>(
            AlignDown<std::uintptr_t>(p, alignment));
        if (aligned != p)
        {
            if (Logger::IsEnabled(LogLevel::Info, DNG_LOGCAT_ALIGNMENT))
            {
                DNG_LOG_INFO(DNG_LOGCAT_ALIGNMENT, "AlignDown adjusted pointer {} -> {} (align={})", (void*)p, (void*)aligned, (size_t)NormalizeAlignment(alignment));
            }
        }
        return reinterpret_cast<void*>(aligned);
    }

    // ---
    // Purpose : Provide const-correct façade for pointer alignment without duplicating logic.
    // Contract: Mirrors mutable overload; never mutates the pointed-to data.
    // Notes   : Casts away const temporarily to reuse core implementation safely.
    // ---
    [[nodiscard]] inline const void* AlignUp(const void* ptr, usize alignment) noexcept
    {
        return const_cast<const void*>(
            AlignUp(const_cast<void*>(ptr), alignment));
    }

    // ---
    // Purpose : Const overload for aligning pointers downward while preserving qualifier.
    // Contract: Equivalent behaviour to mutable version; returns original pointer when already aligned.
    // Notes   : Implementation delegates to mutable helper for arithmetic reuse.
    // ---
    [[nodiscard]] inline const void* AlignDown(const void* ptr, usize alignment) noexcept
    {
        return const_cast<const void*>(
            AlignDown(const_cast<void*>(ptr), alignment));
    }

    // =========================================================================
    // IsAligned helpers
    // =========================================================================
    // ---
    // Purpose : Verify an unsigned integer adheres to the requested alignment.
    // Contract: Accepts unsigned integral types only; alignment normalized before evaluation.
    // Notes   : constexpr-friendly to enable compile-time validation in templates.
    // ---
    template <class T,
              std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int> = 0>
    [[nodiscard]] constexpr bool IsAligned(T value, usize alignment) noexcept
    {
        const T a = static_cast<T>(NormalizeAlignment(alignment));
        return (value & (a - T{ 1 })) == T{ 0 };
    }

    // ---
    // Purpose : Determine whether a pointer satisfies the requested alignment.
    // Contract: Works with null pointers; normalizes alignment before evaluation.
    // Notes   : Misalignment diagnostics are logged only when the category is enabled.
    // ---
    [[nodiscard]] inline bool IsAligned(const void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const bool ok = IsAligned<std::uintptr_t>(p, alignment);
        if (!ok)
        {
            if (Logger::IsEnabled(LogLevel::Warn, DNG_LOGCAT_ALIGNMENT))
            {
                DNG_LOG_WARNING(DNG_LOGCAT_ALIGNMENT, "Pointer {} is NOT aligned to {}", ptr, (size_t)NormalizeAlignment(alignment));
            }
        }
        return ok;
    }

    // =========================================================================
    // Compile-time examples (static_assert)
    // =========================================================================

    // Power-of-two checks
    static_assert(IsPowerOfTwo(1), "1 is power of two");
    static_assert(IsPowerOfTwo(2), "2 is power of two");
    static_assert(IsPowerOfTwo(8), "8 is power of two");
    static_assert(!IsPowerOfTwo(3), "3 is not power of two");
    static_assert(!IsPowerOfTwo(0), "0 is not power of two");

    // NormalizeAlignment behavior
    static_assert(NormalizeAlignment(0) >= alignof(std::max_align_t),
        "Zero alignment maps to at least max_align_t");
    static_assert(NormalizeAlignment(8) == 8,
        "Normalize preserves valid power-of-two");
    static_assert(IsPowerOfTwo(NormalizeAlignment((std::numeric_limits<usize>::max)())),
        "Normalization returns a power-of-two even for extreme inputs");

    // AlignUp / AlignDown integer examples (use UNSIGNED types)
    static_assert(AlignUp<usize>(13, 8) == 16, "13 aligned up to 8 -> 16");
    static_assert(AlignDown<usize>(13, 8) == 8, "13 aligned down to 8 -> 8");
    static_assert(AlignUp<usize>(16, 8) == 16, "16 already aligned up to 8");
    static_assert(AlignDown<usize>(16, 8) == 16, "16 already aligned down to 8");

    static_assert(IsAligned<usize>(16, 8), "16 is aligned to 8");
    static_assert(!IsAligned<usize>(14, 8), "14 is not aligned to 8");

} // namespace dng::core
