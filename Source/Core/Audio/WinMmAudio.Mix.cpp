// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudio.Mix.cpp
// ----------------------------------------------------------------------------
// Purpose : Voice control and software mixing for the WinMM audio backend.
// Contract: No exceptions/RTTI. No hidden allocations in Mix().
// ============================================================================

#include "Core/Audio/WinMmAudioInternal.hpp"

namespace dng::audio
{

AudioCaps WinMmAudio::GetCaps() const noexcept
{
    AudioCaps caps{};
    if (!m_IsInitialized)
    {
        return caps;
    }

    caps.determinism = dng::DeterminismMode::Off;
    caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
    caps.stableMixOrder = true;
    return caps;
}

AudioStatus WinMmAudio::Play(AudioVoiceId voice, const AudioPlayParams& params) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) ||
        voice.slot >= kMaxVoices ||
        !IsValid(params.clip) ||
        !HasClip(params.clip) ||
        !IsValid(params.bus) ||
        !(params.gain >= 0.0f) ||
        !(params.pitch > 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    voiceState = VoiceState{};
    voiceState.clip = params.clip;
    voiceState.targetGain = params.gain;
    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame = params.gain / static_cast<float>(kGainRampFrames);
    if (params.gain <= 0.0f)
    {
        voiceState.currentGain = params.gain;
        voiceState.gainRampFramesRemaining = 0;
        voiceState.gainStepPerFrame = 0.0f;
    }
    voiceState.pitch = params.pitch;
    voiceState.generation = voice.generation;
    voiceState.active = true;
    voiceState.loop = params.loop;
    voiceState.bus = params.bus;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Stop(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::Ok;
    }

    voiceState.paused = false;
    voiceState.stopAfterGainRamp = true;
    voiceState.targetGain = 0.0f;
    if (voiceState.currentGain <= 0.0f)
    {
        ResetVoiceForInvalidClip(voiceState);
        return AudioStatus::Ok;
    }

    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame = -voiceState.currentGain / static_cast<float>(kGainRampFrames);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Pause(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.paused = true;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Resume(AudioVoiceId voice) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.paused = false;
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::Seek(AudioVoiceId voice, dng::u32 frameIndex) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices)
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    if (!IsValid(voiceState.clip) || voiceState.clip.value > kMaxClips)
    {
        return AudioStatus::InvalidArg;
    }

    const ClipState& clip = m_Clips[voiceState.clip.value - 1u];
    if (!clip.valid || clip.channelCount == 0u)
    {
        return AudioStatus::InvalidArg;
    }

    dng::u32 clipFrameCount = 0;
    if (clip.storage == ClipStorageKind::Memory)
    {
        clipFrameCount = clip.sampleCount / static_cast<dng::u32>(clip.channelCount);
    }
    else if (clip.storage == ClipStorageKind::Stream)
    {
        if (clip.streamSlot >= kMaxStreamClips ||
            m_StreamClips == nullptr ||
            !m_StreamClips[clip.streamSlot].valid)
        {
            return AudioStatus::InvalidArg;
        }
        clipFrameCount = m_StreamClips[clip.streamSlot].frameCount;
    }
    else
    {
        return AudioStatus::InvalidArg;
    }

    if (clipFrameCount == 0u || frameIndex >= clipFrameCount)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.frameCursor = static_cast<double>(frameIndex);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::SetGain(AudioVoiceId voice, float gain) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(voice) || voice.slot >= kMaxVoices || !(gain >= 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    VoiceState& voiceState = m_Voices[voice.slot];
    if (!voiceState.active || voiceState.generation != voice.generation)
    {
        return AudioStatus::InvalidArg;
    }

    voiceState.targetGain = gain;
    voiceState.stopAfterGainRamp = false;
    voiceState.gainRampFramesRemaining = kGainRampFrames;
    voiceState.gainStepPerFrame =
        (voiceState.targetGain - voiceState.currentGain) / static_cast<float>(kGainRampFrames);
    return AudioStatus::Ok;
}

AudioStatus WinMmAudio::SetBusGain(AudioBus bus, float gain) noexcept
{
    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (!IsValid(bus) || !(gain >= 0.0f))
    {
        return AudioStatus::InvalidArg;
    }

    m_BusGains[ToBusIndex(bus)] = gain;
    return AudioStatus::Ok;
}

void WinMmAudio::MixVoicesToBuffer(float* outSamples,
                                   dng::u16 outChannelCount,
                                   dng::u32 requestedFrames) noexcept
{
    if (outSamples == nullptr || outChannelCount == 0 || requestedFrames == 0)
    {
        return;
    }

    auto sampleStreamFrame = [this](StreamClipState& streamClip,
                                    dng::u32 frameIndex,
                                    float& outLeft,
                                    float& outRight) noexcept -> bool
    {
        if (!EnsureStreamCache(streamClip, frameIndex))
        {
            return false;
        }

        const dng::u32 localFrame = frameIndex - streamClip.cacheStartFrame;
        if (localFrame >= streamClip.cacheFrameCount)
        {
            return false;
        }

        const dng::u32 base = localFrame * static_cast<dng::u32>(streamClip.channelCount);
        outLeft = detail::Pcm16ToFloat(streamClip.cacheSamples[base]);
        outRight = (streamClip.channelCount > 1u)
            ? detail::Pcm16ToFloat(streamClip.cacheSamples[base + 1u])
            : outLeft;
        return true;
    };

    const double outputSampleRate = static_cast<double>(m_SampleRate);

    for (dng::u32 voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
        VoiceState& voice = m_Voices[voiceIndex];
        if (!voice.active || voice.paused || !IsValid(voice.clip) || voice.clip.value > kMaxClips)
        {
            continue;
        }

        const ClipState& clip = m_Clips[voice.clip.value - 1u];
        if (!clip.valid || clip.channelCount == 0 || clip.sampleRate == 0)
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        StreamClipState* streamState = nullptr;
        dng::u32 clipFrameCount = 0;
        if (clip.storage == ClipStorageKind::Memory)
        {
            clipFrameCount = clip.sampleCount / static_cast<dng::u32>(clip.channelCount);
        }
        else if (clip.storage == ClipStorageKind::Stream)
        {
            if (clip.streamSlot >= kMaxStreamClips || m_StreamClips == nullptr)
            {
                ResetVoiceForInvalidClip(voice);
                continue;
            }

            streamState = &m_StreamClips[clip.streamSlot];
            if (!streamState->valid ||
                streamState->channelCount != clip.channelCount ||
                streamState->sampleRate != clip.sampleRate)
            {
                ResetVoiceForInvalidClip(voice);
                continue;
            }

            clipFrameCount = streamState->frameCount;
        }
        else
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        if (clipFrameCount == 0)
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        const double sourceStep = static_cast<double>(voice.pitch) *
            (static_cast<double>(clip.sampleRate) / outputSampleRate);
        if (!(sourceStep > 0.0))
        {
            ResetVoiceForInvalidClip(voice);
            continue;
        }

        const double clipFrameCountD = static_cast<double>(clipFrameCount);
        for (dng::u32 frame = 0; frame < requestedFrames; ++frame)
        {
            while (voice.frameCursor >= clipFrameCountD)
            {
                if (voice.loop)
                {
                    voice.frameCursor -= clipFrameCountD;
                }
                else
                {
                    ResetVoiceForInvalidClip(voice);
                    break;
                }
            }
            if (!voice.active)
            {
                break;
            }

            dng::u32 srcFrameA = static_cast<dng::u32>(voice.frameCursor);
            if (srcFrameA >= clipFrameCount)
            {
                srcFrameA = clipFrameCount - 1u;
            }
            const float frac = static_cast<float>(voice.frameCursor - static_cast<double>(srcFrameA));

            dng::u32 srcFrameB = srcFrameA + 1u;
            if (srcFrameB >= clipFrameCount)
            {
                srcFrameB = voice.loop ? 0u : srcFrameA;
            }

            float srcLeftA = 0.0f;
            float srcRightA = 0.0f;
            float srcLeftB = 0.0f;
            float srcRightB = 0.0f;
            bool sampleOk = true;

            if (clip.storage == ClipStorageKind::Memory)
            {
                const dng::u32 srcBaseA = clip.sampleOffset + srcFrameA * static_cast<dng::u32>(clip.channelCount);
                const dng::u32 srcBaseB = clip.sampleOffset + srcFrameB * static_cast<dng::u32>(clip.channelCount);

                srcLeftA = detail::Pcm16ToFloat(m_ClipSamplePool[srcBaseA]);
                srcRightA = (clip.channelCount > 1u)
                    ? detail::Pcm16ToFloat(m_ClipSamplePool[srcBaseA + 1u])
                    : srcLeftA;
                srcLeftB = detail::Pcm16ToFloat(m_ClipSamplePool[srcBaseB]);
                srcRightB = (clip.channelCount > 1u)
                    ? detail::Pcm16ToFloat(m_ClipSamplePool[srcBaseB + 1u])
                    : srcLeftB;
            }
            else
            {
                sampleOk = sampleStreamFrame(*streamState, srcFrameA, srcLeftA, srcRightA) &&
                           sampleStreamFrame(*streamState, srcFrameB, srcLeftB, srcRightB);
            }

            if (!sampleOk)
            {
                ResetVoiceForInvalidClip(voice);
                break;
            }

            const float srcLeft = detail::Lerp(srcLeftA, srcLeftB, frac);
            const float srcRight = detail::Lerp(srcRightA, srcRightB, frac);
            const float masterGain = m_BusGains[ToBusIndex(AudioBus::Master)];
            float scopedBusGain = 1.0f;
            if (voice.bus != AudioBus::Master)
            {
                scopedBusGain = m_BusGains[ToBusIndex(voice.bus)];
            }
            const float gain = voice.currentGain * scopedBusGain * masterGain;

            if (outChannelCount == 1u)
            {
                const float mono = (srcLeft + srcRight) * 0.5f * gain;
                outSamples[frame] = detail::ClampUnit(outSamples[frame] + mono);
            }
            else
            {
                const dng::u32 outBase = frame * static_cast<dng::u32>(outChannelCount);
                outSamples[outBase] = detail::ClampUnit(outSamples[outBase] + (srcLeft * gain));
                outSamples[outBase + 1u] = detail::ClampUnit(outSamples[outBase + 1u] + (srcRight * gain));
            }

            if (voice.gainRampFramesRemaining > 0)
            {
                voice.currentGain += voice.gainStepPerFrame;
                --voice.gainRampFramesRemaining;
                if (voice.gainRampFramesRemaining == 0)
                {
                    voice.currentGain = voice.targetGain;
                    voice.gainStepPerFrame = 0.0f;
                }
            }

            if (voice.stopAfterGainRamp &&
                voice.gainRampFramesRemaining == 0 &&
                voice.currentGain <= 0.0f)
            {
                ResetVoiceForInvalidClip(voice);
                break;
            }

            voice.frameCursor += sourceStep;
        }
    }
}

AudioStatus WinMmAudio::Mix(AudioMixParams& params) noexcept
{
    params.writtenSamples = 0;

    if (!m_IsInitialized)
    {
        return AudioStatus::NotSupported;
    }

    if (params.outSamples == nullptr ||
        params.sampleRate != m_SampleRate ||
        params.channelCount != m_ChannelCount ||
        params.requestedFrames == 0)
    {
        if (params.requestedFrames == 0 &&
            params.sampleRate == m_SampleRate &&
            params.channelCount == m_ChannelCount)
        {
            return AudioStatus::Ok;
        }
        return AudioStatus::InvalidArg;
    }

    const dng::u64 requestedSamples64 =
        static_cast<dng::u64>(params.requestedFrames) * static_cast<dng::u64>(params.channelCount);
    if (requestedSamples64 > static_cast<dng::u64>(params.outputCapacitySamples) ||
        requestedSamples64 > static_cast<dng::u64>(detail::MaxU32()) ||
        params.requestedFrames > m_FramesPerBuffer)
    {
        return AudioStatus::InvalidArg;
    }

    const dng::u32 requestedSamples = static_cast<dng::u32>(requestedSamples64);

    dng::i32 deviceBufferIndex = -1;
#if DNG_PLATFORM_WINDOWS
    for (dng::u32 attempt = 0; attempt < kBufferCount; ++attempt)
    {
        const dng::u32 index = (m_NextBufferIndex + attempt) % kBufferCount;
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[index]);
        if (!m_InFlight[index] || ((header->dwFlags & WHDR_DONE) != 0u))
        {
            deviceBufferIndex = static_cast<dng::i32>(index);
            m_InFlight[index] = false;
            m_NextBufferIndex = (index + 1u) % kBufferCount;
            break;
        }
    }
#endif

    dng::i16* deviceSamples = nullptr;
    if (deviceBufferIndex >= 0)
    {
        deviceSamples = m_PcmBuffers[static_cast<dng::u32>(deviceBufferIndex)];
    }
    else
    {
        ++m_UnderrunCount;
    }

    for (dng::u32 i = 0; i < requestedSamples; ++i)
    {
        params.outSamples[i] = 0.0f;
    }

    MixVoicesToBuffer(params.outSamples, params.channelCount, params.requestedFrames);

    if (deviceSamples != nullptr)
    {
        for (dng::u32 i = 0; i < requestedSamples; ++i)
        {
            deviceSamples[i] = detail::FloatToPcm16(params.outSamples[i]);
        }
    }

    params.writtenSamples = requestedSamples;

#if DNG_PLATFORM_WINDOWS
    if (deviceBufferIndex >= 0)
    {
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(m_WaveHeaders[static_cast<dng::u32>(deviceBufferIndex)]);
        header->dwBufferLength = static_cast<DWORD>(requestedSamples * static_cast<dng::u32>(sizeof(dng::i16)));
        header->dwFlags &= ~WHDR_DONE;

        const MMRESULT writeResult =
            ::waveOutWrite(static_cast<HWAVEOUT>(m_Device), header, static_cast<UINT>(sizeof(WAVEHDR)));
        if (writeResult != MMSYSERR_NOERROR)
        {
            ++m_SubmitErrorCount;
            m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = false;
            return AudioStatus::Ok;
        }

        m_InFlight[static_cast<dng::u32>(deviceBufferIndex)] = true;
    }
#endif

    return AudioStatus::Ok;
}

} // namespace dng::audio
