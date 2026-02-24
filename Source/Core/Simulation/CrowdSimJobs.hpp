// ============================================================================
// D-Engine - Source/Core/Simulation/CrowdSimJobs.hpp
// ----------------------------------------------------------------------------
// Purpose : Jobs-friendly crowd stepping that splits acceleration sampling and
//           state integration into deterministic phases.
// Contract: Header-only, no exceptions/RTTI, no allocations. Phase A uses the
//           Jobs contract ParallelFor and writes only caller-owned scratch
//           lanes. Phase B merges in stable increasing index order.
// Notes   : Designed for replay determinism. Uses the same acceleration and
//           integration rules as StepCrowd from CrowdSim.hpp.
// ============================================================================

#pragma once

#include "Core/Contracts/Jobs.hpp"
#include "Core/Simulation/CrowdSim.hpp"

namespace dng::sim
{
    namespace detail
    {
        struct CrowdAccelTaskContext
        {
            const dng::u32* rng = nullptr;
            dng::i32* scratchAccX = nullptr;
            dng::i32* scratchAccY = nullptr;
            dng::u32 tickSalt = 0u;
        };

        inline void RunCrowdAccelTask(void* userData, dng::u32 index) noexcept
        {
            auto* ctx = static_cast<CrowdAccelTaskContext*>(userData);
            if (ctx == nullptr || ctx->rng == nullptr || ctx->scratchAccX == nullptr || ctx->scratchAccY == nullptr)
            {
                return;
            }

            dng::u32 state = ctx->rng[index] ^ ctx->tickSalt ^ (index * 0x85ebca6bu);
            const dng::u32 r0 = detail::NextRandom(state);
            const dng::u32 r1 = detail::NextRandom(state);
            ctx->scratchAccX[index] = static_cast<dng::i32>(r0 % 3u) - 1;
            ctx->scratchAccY[index] = static_cast<dng::i32>(r1 % 3u) - 1;
        }
    } // namespace detail

    // Purpose : Step crowd simulation in jobs-friendly shape with deterministic merge.
    // Contract: No allocations; scratch arrays must point to at least view.count
    //           elements. Phase A writes scratch lanes only. Phase B applies in
    //           ascending index order to preserve replay determinism.
    // Notes   : Equivalent math shape to StepCrowd for NullJobs and deterministic
    //           schedulers that honor index-local writes in Phase A.
    inline void StepCrowdJobs(
        dng::jobs::JobsInterface& jobs,
        CrowdSoAView view,
        const CrowdParams& params,
        dng::u32 tickIndex,
        dng::i32* scratchAccX,
        dng::i32* scratchAccY) noexcept
    {
        if (!detail::IsValidView(view))
        {
            return;
        }

        if (view.count == 0u)
        {
            return;
        }

        if (scratchAccX == nullptr || scratchAccY == nullptr)
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

        detail::CrowdAccelTaskContext taskCtx{};
        taskCtx.rng = view.rng;
        taskCtx.scratchAccX = scratchAccX;
        taskCtx.scratchAccY = scratchAccY;
        taskCtx.tickSalt = tickSalt;

        dng::jobs::ParallelForBody body{};
        body.func = &detail::RunCrowdAccelTask;
        body.userData = &taskCtx;

        dng::jobs::JobCounter counter{};
        dng::jobs::ParallelFor(jobs, view.count, body, counter);
        dng::jobs::WaitForCounter(jobs, counter);

        for (dng::u32 i = 0; i < view.count; ++i)
        {
            const dng::i32 accelX = scratchAccX[i];
            const dng::i32 accelY = scratchAccY[i];

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

            dng::u32 state = view.rng[i] ^ tickSalt ^ (i * 0x85ebca6bu);
            state = detail::NextRandom(state);
            state = detail::NextRandom(state);

            view.posX[i] = static_cast<dng::i32>(nextPosX);
            view.posY[i] = static_cast<dng::i32>(nextPosY);
            view.velX[i] = nextVelX;
            view.velY[i] = nextVelY;
            view.rng[i] = state;
        }
    }
} // namespace dng::sim
