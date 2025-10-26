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
#include <cassert>      // assert (kept for constexpr-safe checks)

#include "Core/Logger.hpp" // DNG_LOG_*

namespace dng::core
{
    // Prefer an unsigned integral for sizes/alignments.
    using usize = std::size_t;

    // =========================================================================
    // Basic predicate: IsPowerOfTwo
    // =========================================================================
    /**
     * @brief Returns true if x is a power of two (and > 0).
     *        Works in constexpr context.
     */
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
        /**
         * @brief Returns the highest power of two representable by `usize`.
         *        Example (64-bit): 1ULL << 63.
         */
        [[nodiscard]] constexpr usize HighestPow2() noexcept
        {
            // `digits` = number of value bits (no sign bit for unsigned types).
            // Example: size_t on 64-bit -> 64, we need 1 << (64-1) = 1<<63.
            return (usize{ 1 } << (std::numeric_limits<usize>::digits - 1));
        }

        /**
         * @brief Returns the next power-of-two >= x with SATURATION.
         *        - If x == 0 -> returns 1.
         *        - If x is already a power-of-two -> returns x.
         *        - Otherwise rounds up to the next power-of-two.
         *        - If rounding up would overflow, returns HighestPow2().
         *
         * Rationale:
         *   When x is extremely large (close to SIZE_MAX), there may be no
         *   representable "next" power-of-two in `usize`. In that case we
         *   saturate to the largest representable power-of-two instead of
         *   overflowing or producing undefined behavior.
         */
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

        /**
         * @brief Add-with-overflow check for UNSIGNED values.
         *        Returns true if (a + b) would overflow.
         */
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
    /**
     * Alignment policy:
     *  - Callers may pass ANY `alignment` (including 0 = "default").
     *  - The engine normalizes it as follows:
     *      1) If alignment == 0 -> use at least alignof(std::max_align_t).
     *      2) Otherwise round UP to the NEXT power-of-two (constexpr).
     *      3) Guarantee the result is >= alignof(std::max_align_t).
     *      4) If "next power-of-two" is not representable (overflow),
     *         we SATURATE to the largest representable power-of-two.
     *
     * Guarantees:
     *  - Result is a power-of-two.
     *  - Result >= alignof(std::max_align_t).
     *  - Result >= 1.
     */
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

    /**
     * @brief Aligns 'value' UP to the next multiple of 'alignment'.
     *        If already aligned, returns value.
     *        Overflow-safe: asserts in debug if value + (alignment-1) would overflow
     *        and clamps to max in that case.
     */
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
            assert(!"AlignUp overflow: value + (alignment-1) exceeds max");
            return (std::numeric_limits<T>::max)(); // clamp (defined, not UB)
        }

        const T aligned = (value + mask) & ~mask;
        return aligned;
    }

    /**
     * @brief Aligns 'value' DOWN to the previous multiple of 'alignment'.
     *        No overflow risk.
     */
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
    /**
     * @brief Aligns a pointer UP to the next multiple of 'alignment'.
     *        Emits a runtime log if the pointer was not aligned and had to move.
     */
    [[nodiscard]] inline void* AlignUp(void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const std::uintptr_t aligned = static_cast<std::uintptr_t>(
            AlignUp<std::uintptr_t>(p, alignment));
        if (aligned != p)
        {
            DNG_LOG_INFO("Alignment", "AlignUp adjusted pointer {} -> {} (align={})", (void*)p, (void*)aligned, (size_t)NormalizeAlignment(alignment));
        }
        return reinterpret_cast<void*>(aligned);
    }

    /**
     * @brief Aligns a pointer DOWN to the previous multiple of 'alignment'.
     *        Emits a runtime log if the pointer was not aligned and had to move.
     */
    [[nodiscard]] inline void* AlignDown(void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const std::uintptr_t aligned = static_cast<std::uintptr_t>(
            AlignDown<std::uintptr_t>(p, alignment));
        if (aligned != p)
        {
            DNG_LOG_INFO("Alignment", "AlignDown adjusted pointer {} -> {} (align={})", (void*)p, (void*)aligned, (size_t)NormalizeAlignment(alignment));
        }
        return reinterpret_cast<void*>(aligned);
    }

    // Const-pointer overloads
    [[nodiscard]] inline const void* AlignUp(const void* ptr, usize alignment) noexcept
    {
        return const_cast<const void*>(
            AlignUp(const_cast<void*>(ptr), alignment));
    }

    [[nodiscard]] inline const void* AlignDown(const void* ptr, usize alignment) noexcept
    {
        return const_cast<const void*>(
            AlignDown(const_cast<void*>(ptr), alignment));
    }

    // =========================================================================
    // IsAligned helpers
    // =========================================================================
    /**
     * @brief Checks if an UNSIGNED integer value is aligned to 'alignment'.
     */
    template <class T,
              std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>, int> = 0>
    [[nodiscard]] constexpr bool IsAligned(T value, usize alignment) noexcept
    {
        const T a = static_cast<T>(NormalizeAlignment(alignment));
        return (value & (a - T{ 1 })) == T{ 0 };
    }

    /**
     * @brief Checks if a pointer is aligned to 'alignment'.
     *        Logs at verbose level when misaligned.
     */
    [[nodiscard]] inline bool IsAligned(const void* ptr, usize alignment) noexcept
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr);
        const bool ok = IsAligned<std::uintptr_t>(p, alignment);
        if (!ok)
        {
            DNG_LOG_WARNING("Alignment", "Pointer {} is NOT aligned to {}", ptr, (size_t)NormalizeAlignment(alignment));
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
    static_assert(NormalizeAlignment(32) == 32,
        "Normalize preserves valid power-of-two >= max_align_t");
    static_assert(IsPowerOfTwo(NormalizeAlignment((std::numeric_limits<usize>::max)())),
        "Normalization returns a power-of-two even for extreme inputs");

    // AlignUp / AlignDown integer examples (use UNSIGNED types)
    // Note: Use alignment >= alignof(std::max_align_t) for portable tests
    static_assert(AlignUp<usize>(29, 32) == 32, "29 aligned up to 32 -> 32");
    static_assert(AlignDown<usize>(29, 32) == 0, "29 aligned down to 32 -> 0");
    static_assert(AlignUp<usize>(32, 32) == 32, "32 already aligned up to 32");
    static_assert(AlignDown<usize>(32, 32) == 32, "32 already aligned down to 32");

    static_assert(IsAligned<usize>(32, 32), "32 is aligned to 32");
    static_assert(!IsAligned<usize>(30, 32), "30 is not aligned to 32");

} // namespace dng::core
