// ============================================================================
// D-Engine - Source/Core/Simulation/CrowdSubmit.hpp
// ----------------------------------------------------------------------------
// Purpose : Build renderer contract instances from crowd SoA simulation data
//           using caller-owned output buffers.
// Contract: Header-only, no exceptions/RTTI, no allocations. Output pointers
//           are non-owning and must remain valid for the caller usage window.
// Notes   : Uses the existing renderer contract types only (RenderInstance and
//           FrameSubmission paths). Translation maps 2D crowd positions to
//           world-space X/Y with Z fixed to 0.
// ============================================================================

#pragma once

#include "Core/Contracts/Renderer.hpp"
#include "Core/Simulation/CrowdSim.hpp"

namespace dng::sim
{
    // Purpose : Convert crowd agent positions into render instances.
    // Contract: Writes up to min(view.count, outCapacity) instances and returns
    //           the count written. No allocations, logging, or I/O.
    // Notes   : meshUserData/materialUserData are copied into handle values.
    [[nodiscard]] inline dng::u32 BuildCrowdRenderInstances(
        const CrowdSoAView view,
        dng::render::RenderInstance* outInstances,
        dng::u32 outCapacity,
        dng::u32 meshUserData,
        dng::u32 materialUserData) noexcept
    {
        if (view.count == 0u || outCapacity == 0u || outInstances == nullptr)
        {
            return 0u;
        }

        if (view.posX == nullptr || view.posY == nullptr)
        {
            return 0u;
        }

        const dng::u32 writeCount = (view.count < outCapacity) ? view.count : outCapacity;
        const dng::render::MeshHandle meshHandle(meshUserData);
        const dng::render::MaterialHandle materialHandle(materialUserData);
        const dng::render::PipelineHandle pipelineHandle = dng::render::PipelineHandle::Invalid();
        const dng::render::TextureHandle textureHandle = dng::render::TextureHandle::Invalid();

        for (dng::u32 i = 0; i < writeCount; ++i)
        {
            dng::render::RenderInstance instance{};
            instance.worldMatrix = dng::Mat4f::Identity();
            instance.worldMatrix.m[3][0] = static_cast<dng::float32>(view.posX[i]);
            instance.worldMatrix.m[3][1] = static_cast<dng::float32>(view.posY[i]);
            instance.worldMatrix.m[3][2] = 0.0f;
            instance.mesh = meshHandle;
            instance.material = materialHandle;
            instance.pipeline = pipelineHandle;
            instance.overrideTexture = textureHandle;
            instance.viewMask = 0xFFFFFFFFu;
            instance.instanceUserData = i;
            outInstances[i] = instance;
        }

        return writeCount;
    }
} // namespace dng::sim

