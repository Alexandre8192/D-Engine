// ============================================================================
// D-Engine - Source/Core/Simulation/CrowdSim.hpp
// ----------------------------------------------------------------------------
// Purpose : Provide a tiny deterministic CPU-only crowd simulation kernel over
//           caller-owned SoA buffers for replay-focused smoke and benchmark use.
// Contract: Header-only, no exceptions/RTTI, allocation-free hot path. All
//           buffers are non-owning and caller-managed; functions are noexcept
//           and deterministic for equal inputs on the same platform/compiler.
// Notes   : Uses integer-only math and a per-agent u32 LCG state. Overflow is
//           only relied on for unsigned arithmetic where wraparound is defined.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <type_traits>

namespace dng::sim
{
    struct CrowdSoAView
    {
        dng::i32* posX = nullptr;
        dng::i32* posY = nullptr;
        dng::i32* velX = nullptr;
        dng::i32* velY = nullptr;
        dng::u32* rng  = nullptr;
        dng::u32  count = 0;
    };

    struct CrowdParams
    {
        dng::i32 worldMinX = -1024;
        dng::i32 worldMaxX = 1024;
        dng::i32 worldMinY = -1024;
        dng::i32 worldMaxY = 1024;
        dng::i32 maxSpeed  = 8;
        dng::u32 seed      = 1u;
    };

    static_assert(std::is_trivially_copyable_v<CrowdSoAView>);
    static_assert(std::is_trivially_copyable_v<CrowdParams>);

    namespace detail
    {
        constexpr dng::u32 kLcgMul = 1664525u;
        constexpr dng::u32 kLcgInc = 1013904223u;

        constexpr dng::u64 kFnvOffset = 0xcbf29ce484222325ull;
        constexpr dng::u64 kFnvPrime  = 0x00000100000001b3ull;

        [[nodiscard]] inline bool IsValidView(const CrowdSoAView& view) noexcept
        {
            if (view.count == 0u)
            {
                return true;
            }

            return view.posX != nullptr &&
                   view.posY != nullptr &&
                   view.velX != nullptr &&
                   view.velY != nullptr &&
                   view.rng  != nullptr;
        }

        inline void CanonicalizeBounds(const CrowdParams& params,
                                       dng::i32& minX,
                                       dng::i32& maxX,
                                       dng::i32& minY,
                                       dng::i32& maxY) noexcept
        {
            minX = (params.worldMinX <= params.worldMaxX) ? params.worldMinX : params.worldMaxX;
            maxX = (params.worldMinX <= params.worldMaxX) ? params.worldMaxX : params.worldMinX;
            minY = (params.worldMinY <= params.worldMaxY) ? params.worldMinY : params.worldMaxY;
            maxY = (params.worldMinY <= params.worldMaxY) ? params.worldMaxY : params.worldMinY;
        }

        [[nodiscard]] inline dng::i32 NormalizeMaxSpeed(dng::i32 maxSpeed) noexcept
        {
            const dng::i64 raw = static_cast<dng::i64>(maxSpeed);
            const dng::i64 abs = (raw < 0) ? -raw : raw;
            constexpr dng::i64 kI32Max = 0x7fffffffll;
            return static_cast<dng::i32>((abs > kI32Max) ? kI32Max : abs);
        }

        [[nodiscard]] inline dng::u32 NextRandom(dng::u32& state) noexcept
        {
            state = state * kLcgMul + kLcgInc;
            return state;
        }

        [[nodiscard]] inline dng::i32 SampleRange(dng::u32 randomValue, dng::i32 minValue, dng::i32 maxValue) noexcept
        {
            const dng::i64 min64 = static_cast<dng::i64>(minValue);
            const dng::u64 span = static_cast<dng::u64>(static_cast<dng::i64>(maxValue) - min64) + 1ull;
            const dng::u64 offset = static_cast<dng::u64>(randomValue) % span;
            return static_cast<dng::i32>(min64 + static_cast<dng::i64>(offset));
        }

        [[nodiscard]] inline dng::i32 ClampToMaxAbs(dng::i64 value, dng::i32 maxAbs) noexcept
        {
            const dng::i64 limit = static_cast<dng::i64>(maxAbs);
            if (value > limit)
            {
                return maxAbs;
            }
            if (value < -limit)
            {
                return static_cast<dng::i32>(-limit);
            }
            return static_cast<dng::i32>(value);
        }

        inline void HashByte(dng::u64& hash, dng::u8 value) noexcept
        {
            hash ^= static_cast<dng::u64>(value);
            hash *= kFnvPrime;
        }

        inline void HashU32(dng::u64& hash, dng::u32 value) noexcept
        {
            HashByte(hash, static_cast<dng::u8>((value >> 0u)  & 0xffu));
            HashByte(hash, static_cast<dng::u8>((value >> 8u)  & 0xffu));
            HashByte(hash, static_cast<dng::u8>((value >> 16u) & 0xffu));
            HashByte(hash, static_cast<dng::u8>((value >> 24u) & 0xffu));
        }

        inline void HashI32(dng::u64& hash, dng::i32 value) noexcept
        {
            HashU32(hash, static_cast<dng::u32>(value));
        }
    } // namespace detail

    // Purpose : Initialize caller-owned SoA buffers with deterministic integer
    //           positions, velocities, and per-agent RNG states.
    // Contract: No allocations; no side effects outside view buffers. Returns
    //           immediately when the view is invalid.
    // Notes   : Seed mixing uses only u32 wraparound operations.
    inline void InitCrowd(CrowdSoAView view, const CrowdParams& params) noexcept
    {
        if (!detail::IsValidView(view))
        {
            return;
        }

        dng::i32 minX = 0;
        dng::i32 maxX = 0;
        dng::i32 minY = 0;
        dng::i32 maxY = 0;
        detail::CanonicalizeBounds(params, minX, maxX, minY, maxY);

        const dng::i32 maxSpeed = detail::NormalizeMaxSpeed(params.maxSpeed);

        for (dng::u32 i = 0; i < view.count; ++i)
        {
            dng::u32 state = params.seed ^ (i * 0x9e3779b9u + 0x85ebca6bu);

            const dng::u32 rx = detail::NextRandom(state);
            const dng::u32 ry = detail::NextRandom(state);
            const dng::u32 rvx = detail::NextRandom(state);
            const dng::u32 rvy = detail::NextRandom(state);

            view.posX[i] = detail::SampleRange(rx, minX, maxX);
            view.posY[i] = detail::SampleRange(ry, minY, maxY);
            view.velX[i] = detail::ClampToMaxAbs(static_cast<dng::i64>(static_cast<dng::i32>(rvx % 3u)) - 1ll, maxSpeed);
            view.velY[i] = detail::ClampToMaxAbs(static_cast<dng::i64>(static_cast<dng::i32>(rvy % 3u)) - 1ll, maxSpeed);
            view.rng[i] = state;
        }
    }

    // Purpose : Execute one deterministic fixed-step update over all agents.
    // Contract: No allocations and no external state access. Deterministic for
    //           equal inputs on the same platform/compiler.
    // Notes   : Bounds are axis-aligned and use bounce-on-contact behavior.
    inline void StepCrowd(CrowdSoAView view, const CrowdParams& params, dng::u32 tickIndex) noexcept
    {
        if (!detail::IsValidView(view))
        {
            return;
        }

        dng::i32 minX = 0;
        dng::i32 maxX = 0;
        dng::i32 minY = 0;
        dng::i32 maxY = 0;
        detail::CanonicalizeBounds(params, minX, maxX, minY, maxY);

        const dng::i32 maxSpeed = detail::NormalizeMaxSpeed(params.maxSpeed);
        const dng::u32 tickSalt = tickIndex * 0x9e3779b9u + 0x7f4a7c15u;

        for (dng::u32 i = 0; i < view.count; ++i)
        {
            dng::u32 state = view.rng[i] ^ tickSalt ^ (i * 0x85ebca6bu);

            const dng::u32 r0 = detail::NextRandom(state);
            const dng::u32 r1 = detail::NextRandom(state);

            const dng::i32 accelX = static_cast<dng::i32>(r0 % 3u) - 1;
            const dng::i32 accelY = static_cast<dng::i32>(r1 % 3u) - 1;

            dng::i32 nextVelX = detail::ClampToMaxAbs(static_cast<dng::i64>(view.velX[i]) + static_cast<dng::i64>(accelX), maxSpeed);
            dng::i32 nextVelY = detail::ClampToMaxAbs(static_cast<dng::i64>(view.velY[i]) + static_cast<dng::i64>(accelY), maxSpeed);

            dng::i64 nextPosX = static_cast<dng::i64>(view.posX[i]) + static_cast<dng::i64>(nextVelX);
            dng::i64 nextPosY = static_cast<dng::i64>(view.posY[i]) + static_cast<dng::i64>(nextVelY);

            if (nextPosX < static_cast<dng::i64>(minX))
            {
                nextPosX = static_cast<dng::i64>(minX);
                nextVelX = static_cast<dng::i32>(-static_cast<dng::i64>(nextVelX));
            }
            else if (nextPosX > static_cast<dng::i64>(maxX))
            {
                nextPosX = static_cast<dng::i64>(maxX);
                nextVelX = static_cast<dng::i32>(-static_cast<dng::i64>(nextVelX));
            }

            if (nextPosY < static_cast<dng::i64>(minY))
            {
                nextPosY = static_cast<dng::i64>(minY);
                nextVelY = static_cast<dng::i32>(-static_cast<dng::i64>(nextVelY));
            }
            else if (nextPosY > static_cast<dng::i64>(maxY))
            {
                nextPosY = static_cast<dng::i64>(maxY);
                nextVelY = static_cast<dng::i32>(-static_cast<dng::i64>(nextVelY));
            }

            view.posX[i] = static_cast<dng::i32>(nextPosX);
            view.posY[i] = static_cast<dng::i32>(nextPosY);
            view.velX[i] = nextVelX;
            view.velY[i] = nextVelY;
            view.rng[i]  = state;
        }
    }

    // Purpose : Produce a deterministic 64-bit hash of the crowd SoA state.
    // Contract: Allocation-free hash over all agent lanes and count.
    // Notes   : Uses byte-wise FNV-1a over explicit integer fields.
    [[nodiscard]] inline dng::u64 HashCrowd(const CrowdSoAView view) noexcept
    {
        if (!detail::IsValidView(view))
        {
            return 0ull;
        }

        dng::u64 hash = detail::kFnvOffset;
        detail::HashU32(hash, view.count);

        for (dng::u32 i = 0; i < view.count; ++i)
        {
            detail::HashI32(hash, view.posX[i]);
            detail::HashI32(hash, view.posY[i]);
            detail::HashI32(hash, view.velX[i]);
            detail::HashI32(hash, view.velY[i]);
            detail::HashU32(hash, view.rng[i]);
        }

        return hash;
    }
} // namespace dng::sim
