// ============================================================================
// D-Engine - tests/Smoke/Subsystems/CrowdRendererSubmission_smoke.cpp
// ----------------------------------------------------------------------------
// Purpose : Validate deterministic crowd-to-renderer submission using existing
//           renderer contract types and the NullRenderer backend.
// Contract: No exceptions/RTTI; no dynamic allocation in the step/submit loop.
//           Returns 0 on success and non-zero on failure.
// Notes   : Uses fixed static buffers for crowd SoA and render instances.
// ============================================================================

#include "Core/Renderer/NullRenderer.hpp"
#include "Core/Simulation/CrowdSim.hpp"
#include "Core/Simulation/CrowdSubmit.hpp"

namespace
{
    constexpr dng::u32 kAgentCount = 2000u;
    constexpr dng::u32 kTickCount = 16u;
    constexpr dng::u64 kHashOffset = 0xcbf29ce484222325ull;
    constexpr dng::u64 kHashPrime = 0x00000100000001b3ull;

    inline void HashByte(dng::u64& hash, dng::u8 value) noexcept
    {
        hash ^= static_cast<dng::u64>(value);
        hash *= kHashPrime;
    }

    inline void HashU32(dng::u64& hash, dng::u32 value) noexcept
    {
        HashByte(hash, static_cast<dng::u8>((value >> 0u) & 0xffu));
        HashByte(hash, static_cast<dng::u8>((value >> 8u) & 0xffu));
        HashByte(hash, static_cast<dng::u8>((value >> 16u) & 0xffu));
        HashByte(hash, static_cast<dng::u8>((value >> 24u) & 0xffu));
    }

    inline void HashU64(dng::u64& hash, dng::u64 value) noexcept
    {
        HashU32(hash, static_cast<dng::u32>(value & 0xffffffffull));
        HashU32(hash, static_cast<dng::u32>((value >> 32ull) & 0xffffffffull));
    }

    inline void HashI32(dng::u64& hash, dng::i32 value) noexcept
    {
        HashU32(hash, static_cast<dng::u32>(value));
    }

    bool RunSubmissionPass(dng::u64& outHash) noexcept
    {
        static dng::i32 posX[kAgentCount];
        static dng::i32 posY[kAgentCount];
        static dng::i32 velX[kAgentCount];
        static dng::i32 velY[kAgentCount];
        static dng::u32 rng[kAgentCount];
        static dng::render::RenderInstance instances[kAgentCount];

        dng::sim::CrowdSoAView view{};
        view.posX = posX;
        view.posY = posY;
        view.velX = velX;
        view.velY = velY;
        view.rng = rng;
        view.count = kAgentCount;

        dng::sim::CrowdParams params{};
        params.worldMinX = -4096;
        params.worldMaxX = 4096;
        params.worldMinY = -4096;
        params.worldMaxY = 4096;
        params.maxSpeed = 7;
        params.seed = 0x5a17b3cdu;

        dng::sim::InitCrowd(view, params);

        dng::render::NullRenderer backend{};
        dng::render::RendererInterface renderer = dng::render::MakeNullRendererInterface(backend);

        dng::render::RenderView renderView{};
        renderView.width = 1280u;
        renderView.height = 720u;
        renderView.viewId = 1u;

        dng::render::FrameSubmission submission{};
        submission.views = &renderView;
        submission.viewCount = 1u;
        submission.deltaTimeSec = 1.0f / 60.0f;

        dng::u64 hash = kHashOffset;
        for (dng::u32 tick = 0; tick < kTickCount; ++tick)
        {
            dng::sim::StepCrowd(view, params, tick);
            const dng::u32 instanceCount = dng::sim::BuildCrowdRenderInstances(
                view,
                instances,
                kAgentCount,
                101u,
                202u);
            if (instanceCount != kAgentCount)
            {
                return false;
            }

            submission.instances = instances;
            submission.instanceCount = instanceCount;
            submission.frameIndex = static_cast<dng::u64>(tick);

            dng::render::BeginFrame(renderer, submission);
            dng::render::SubmitInstances(renderer, instances, instanceCount);
            dng::render::EndFrame(renderer);

            if (backend.width != renderView.width || backend.height != renderView.height)
            {
                return false;
            }

            if (instances[0].worldMatrix.m[3][0] != static_cast<dng::float32>(view.posX[0]) ||
                instances[0].worldMatrix.m[3][1] != static_cast<dng::float32>(view.posY[0]))
            {
                return false;
            }

            HashU64(hash, dng::sim::HashCrowd(view));
            HashU32(hash, instanceCount);
            HashI32(hash, static_cast<dng::i32>(instances[instanceCount - 1u].instanceUserData));
            HashI32(hash, static_cast<dng::i32>(view.posX[instanceCount - 1u]));
            HashI32(hash, static_cast<dng::i32>(view.posY[instanceCount - 1u]));
        }

        outHash = hash;
        return true;
    }
} // namespace

int RunCrowdRendererSubmissionSmoke()
{
    dng::u64 first = 0ull;
    dng::u64 second = 0ull;

    if (!RunSubmissionPass(first))
    {
        return 1;
    }

    if (!RunSubmissionPass(second))
    {
        return 2;
    }

    return (first == second) ? 0 : 3;
}

