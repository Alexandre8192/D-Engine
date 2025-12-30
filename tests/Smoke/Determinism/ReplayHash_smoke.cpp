// ============================================================================
// D-Engine - tests/Smoke/Determinism/ReplayHash_smoke.cpp
// ----------------------------------------------------------------------------
// Purpose : Minimal replay determinism smoke: run a fixed-step loop twice and
//           ensure the resulting hash matches.
// Contract: No exceptions/RTTI; deterministic seed and math only; runtime < 1s.
// Notes   : Uses a tiny LCG and FNV-1a hash over POD state to avoid hidden
//           allocations or I/O. Returns 0 on pass, non-zero on mismatch.
// ============================================================================

#include <cstdint>
#include <cstddef>

namespace
{
    // Simple 64-bit FNV-1a helper.
    constexpr std::uint64_t Fnv1a64(const std::uint8_t* data, std::size_t size) noexcept
    {
        constexpr std::uint64_t kOffset = 0xcbf29ce484222325ull;
        constexpr std::uint64_t kPrime  = 0x00000100000001b3ull;
        std::uint64_t hash = kOffset;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<std::uint64_t>(data[i]);
            hash *= kPrime;
        }
        return hash;
    }

    // Deterministic LCG (Numerical Recipes constants).
    struct Lcg
    {
        std::uint64_t state;

        constexpr explicit Lcg(std::uint64_t seed) noexcept : state(seed) {}

        constexpr std::uint32_t Next() noexcept
        {
            state = state * 6364136223846793005ull + 1ull;
            return static_cast<std::uint32_t>(state >> 32);
        }
    };

    struct PodState
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t tick;
    };

    constexpr std::uint64_t RunSimulation(std::uint64_t seed, std::uint32_t ticks) noexcept
    {
        Lcg rng(seed);
        PodState state{0u, 0u, 0u};

        for (std::uint32_t i = 0; i < ticks; ++i)
        {
            // Fixed-step deterministic update.
            const std::uint32_t r = rng.Next();
            state.x ^= r + i * 0x9e3779b9u;
            state.y += (r << 1) ^ (state.x >> 3);
            state.tick = i;
        }

        return Fnv1a64(reinterpret_cast<const std::uint8_t*>(&state), sizeof(state));
    }
}

int RunDeterminismReplaySmoke()
{
    constexpr std::uint64_t seed = 0x1234abcdULL;
    constexpr std::uint32_t kTicks = 256u;

    const std::uint64_t first = RunSimulation(seed, kTicks);
    const std::uint64_t second = RunSimulation(seed, kTicks);

    return (first == second) ? 0 : 1;
}
