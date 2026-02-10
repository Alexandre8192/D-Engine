// ============================================================================
// D-Engine - Source/Core/Audio/NullAudio.hpp
// ----------------------------------------------------------------------------
// Purpose : Minimal audio backend that satisfies the audio contract without
//           producing audible output. Useful for tests, tools, and CI.
// Contract: Header-only, no exceptions/RTTI, no allocations, deterministic.
//           Writes zeroed samples when a valid output buffer is provided.
// Notes   : Behaves as a pull-only mixer stub and validates basic arguments.
// ============================================================================

#pragma once

#include "Core/Contracts/Audio.hpp"

namespace dng::audio
{
    struct NullAudio
    {
        dng::u64 lastFrameIndex = 0;

        [[nodiscard]] constexpr AudioCaps GetCaps() const noexcept
        {
            AudioCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableMixOrder = true;
            return caps;
        }

        [[nodiscard]] AudioStatus Mix(AudioMixParams& params) noexcept
        {
            params.writtenSamples = 0;

            if (params.sampleRate == 0 || params.channelCount == 0)
            {
                return AudioStatus::InvalidArg;
            }

            if (params.requestedFrames == 0)
            {
                lastFrameIndex = params.frameIndex;
                return AudioStatus::Ok;
            }

            if (params.outSamples == nullptr)
            {
                return AudioStatus::InvalidArg;
            }

            const dng::u64 requestedSamples64 =
                static_cast<dng::u64>(params.requestedFrames) * static_cast<dng::u64>(params.channelCount);
            if (requestedSamples64 > static_cast<dng::u64>(params.outputCapacitySamples) ||
                requestedSamples64 > static_cast<dng::u64>(~dng::u32{0}))
            {
                return AudioStatus::InvalidArg;
            }

            const dng::u32 requestedSamples = static_cast<dng::u32>(requestedSamples64);
            for (dng::u32 i = 0; i < requestedSamples; ++i)
            {
                params.outSamples[i] = 0.0f;
            }

            params.writtenSamples = requestedSamples;
            lastFrameIndex = params.frameIndex;
            return AudioStatus::Ok;
        }
    };

    static_assert(AudioBackend<NullAudio>, "NullAudio must satisfy audio backend concept.");

    [[nodiscard]] inline AudioInterface MakeNullAudioInterface(NullAudio& backend) noexcept
    {
        return MakeAudioInterface(backend);
    }

} // namespace dng::audio
