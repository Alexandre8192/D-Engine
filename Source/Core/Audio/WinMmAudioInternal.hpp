// ============================================================================
// D-Engine - Source/Core/Audio/WinMmAudioInternal.hpp
// ----------------------------------------------------------------------------
// Purpose : Shared internal helpers for the WinMM audio backend TUs.
// Contract: Private to backend implementation; no public API promises here.
// Notes   : Keeps parsing/mixing primitives consistent while letting the main
//           backend implementation live in multiple focused translation units.
// ============================================================================

#pragma once

#include "Core/Audio/WinMmAudio.hpp"
#include "Core/Platform/PlatformDefines.hpp"
#include "Core/Platform/WindowsApi.hpp"

#include <cstdlib>
#include <cstring>

#if DNG_PLATFORM_WINDOWS
    #include <mmsystem.h>
    #pragma comment(lib, "winmm.lib")
#endif

namespace dng::audio::detail
{
    [[nodiscard]] constexpr dng::u32 MaxU32() noexcept
    {
        return ~dng::u32{ 0 };
    }

    [[nodiscard]] inline dng::u16 ReadLe16(const dng::u8* data) noexcept
    {
        return static_cast<dng::u16>(data[0]) |
               static_cast<dng::u16>(static_cast<dng::u16>(data[1]) << 8u);
    }

    [[nodiscard]] inline dng::u32 ReadLe32(const dng::u8* data) noexcept
    {
        return static_cast<dng::u32>(data[0]) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[1]) << 8u) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[2]) << 16u) |
               static_cast<dng::u32>(static_cast<dng::u32>(data[3]) << 24u);
    }

    [[nodiscard]] inline dng::i16 FloatToPcm16(float value) noexcept
    {
        float clamped = value;
        if (clamped > 1.0f)
        {
            clamped = 1.0f;
        }
        else if (clamped < -1.0f)
        {
            clamped = -1.0f;
        }

        constexpr float kScale = 32767.0f;
        return static_cast<dng::i16>(static_cast<dng::i32>(clamped * kScale));
    }

    [[nodiscard]] inline float Pcm16ToFloat(dng::i16 value) noexcept
    {
        constexpr float kInvScale = 1.0f / 32768.0f;
        return static_cast<float>(value) * kInvScale;
    }

    [[nodiscard]] inline float ClampUnit(float value) noexcept
    {
        if (value > 1.0f)
        {
            return 1.0f;
        }
        if (value < -1.0f)
        {
            return -1.0f;
        }
        return value;
    }

    [[nodiscard]] inline float Lerp(float a, float b, float t) noexcept
    {
        return a + ((b - a) * t);
    }

    struct WavPcm16Info
    {
        dng::u16 channelCount = 0;
        dng::u32 sampleRate = 0;
        dng::u64 dataOffsetBytes = 0;
        dng::u32 dataSizeBytes = 0;
    };

    [[nodiscard]] inline AudioStatus MapFsStatusToAudioStatus(fs::FsStatus status) noexcept
    {
        switch (status)
        {
            case fs::FsStatus::Ok:           return AudioStatus::Ok;
            case fs::FsStatus::InvalidArg:   return AudioStatus::InvalidArg;
            case fs::FsStatus::NotSupported: return AudioStatus::NotSupported;
            case fs::FsStatus::UnknownError: return AudioStatus::UnknownError;
            case fs::FsStatus::NotFound:
            case fs::FsStatus::AccessDenied: return AudioStatus::NotSupported;
            default:                         return AudioStatus::UnknownError;
        }
    }

    [[nodiscard]] inline bool IsSupportedPcmFormat(dng::u16 fmtTag,
                                                   dng::u16 channels,
                                                   dng::u32 sampleRate,
                                                   dng::u16 bitsPerSample) noexcept
    {
        return fmtTag == 1u &&
               bitsPerSample == 16u &&
               (channels == 1u || channels == 2u) &&
               sampleRate != 0u;
    }

    [[nodiscard]] inline AudioStatus ParseWavPcm16FromMemory(const dng::u8* fileData,
                                                             dng::u32 fileSizeBytes,
                                                             WavPcm16Info& outInfo) noexcept
    {
        outInfo = WavPcm16Info{};
        if (fileData == nullptr || fileSizeBytes < 12u)
        {
            return AudioStatus::InvalidArg;
        }

        if (std::memcmp(fileData, "RIFF", 4) != 0 ||
            std::memcmp(&fileData[8], "WAVE", 4) != 0)
        {
            return AudioStatus::InvalidArg;
        }

        bool fmtFound = false;
        dng::u16 fmtTag = 0;
        dng::u16 fmtChannels = 0;
        dng::u32 fmtSampleRate = 0;
        dng::u16 fmtBitsPerSample = 0;

        dng::u32 cursor = 12u;
        while (cursor + 8u <= fileSizeBytes)
        {
            const dng::u8* chunkHeader = &fileData[cursor];
            const dng::u32 chunkSize = ReadLe32(&chunkHeader[4]);
            cursor += 8u;

            if (chunkSize > (fileSizeBytes - cursor))
            {
                return AudioStatus::InvalidArg;
            }

            const dng::u8* chunkData = &fileData[cursor];
            const bool hasPadByte = (chunkSize & 1u) != 0u;

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u)
                {
                    return AudioStatus::InvalidArg;
                }

                fmtTag = ReadLe16(&chunkData[0]);
                fmtChannels = ReadLe16(&chunkData[2]);
                fmtSampleRate = ReadLe32(&chunkData[4]);
                fmtBitsPerSample = ReadLe16(&chunkData[14]);
                fmtFound = true;
            }
            else if (std::memcmp(chunkHeader, "data", 4) == 0)
            {
                if (!fmtFound ||
                    !IsSupportedPcmFormat(fmtTag, fmtChannels, fmtSampleRate, fmtBitsPerSample) ||
                    (chunkSize % 2u) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                const dng::u32 sampleCount = chunkSize / 2u;
                if (sampleCount == 0u ||
                    (sampleCount % static_cast<dng::u32>(fmtChannels)) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                outInfo.channelCount = fmtChannels;
                outInfo.sampleRate = fmtSampleRate;
                outInfo.dataOffsetBytes = cursor;
                outInfo.dataSizeBytes = chunkSize;
                return AudioStatus::Ok;
            }

            const dng::u32 nextCursor = cursor + chunkSize + (hasPadByte ? 1u : 0u);
            if (nextCursor < cursor || nextCursor > fileSizeBytes)
            {
                return AudioStatus::InvalidArg;
            }
            cursor = nextCursor;
        }

        return AudioStatus::InvalidArg;
    }

    [[nodiscard]] inline bool ReadFileRangeExact(fs::FileSystemInterface& fileSystem,
                                                 fs::PathView path,
                                                 dng::u64 offsetBytes,
                                                 void* dst,
                                                 dng::u64 dstSizeBytes,
                                                 fs::FsStatus& outStatus) noexcept
    {
        outStatus = fs::FsStatus::UnknownError;
        if (dst == nullptr && dstSizeBytes != 0u)
        {
            outStatus = fs::FsStatus::InvalidArg;
            return false;
        }

        dng::u64 bytesRead = 0;
        outStatus = fs::ReadFileRange(fileSystem, path, offsetBytes, dst, dstSizeBytes, bytesRead);
        return outStatus == fs::FsStatus::Ok && bytesRead == dstSizeBytes;
    }

    [[nodiscard]] inline AudioStatus ParseWavPcm16FromFile(fs::FileSystemInterface& fileSystem,
                                                           fs::PathView path,
                                                           WavPcm16Info& outInfo) noexcept
    {
        outInfo = WavPcm16Info{};

        dng::u64 fileSize = 0;
        const fs::FsStatus sizeStatus = fs::FileSize(fileSystem, path, fileSize);
        if (sizeStatus != fs::FsStatus::Ok)
        {
            return MapFsStatusToAudioStatus(sizeStatus);
        }

        if (fileSize < 12u)
        {
            return AudioStatus::InvalidArg;
        }

        dng::u8 riffHeader[12]{};
        fs::FsStatus readStatus = fs::FsStatus::UnknownError;
        if (!ReadFileRangeExact(fileSystem, path, 0u, riffHeader, sizeof(riffHeader), readStatus))
        {
            return (readStatus == fs::FsStatus::Ok)
                ? AudioStatus::InvalidArg
                : MapFsStatusToAudioStatus(readStatus);
        }

        if (std::memcmp(riffHeader, "RIFF", 4) != 0 ||
            std::memcmp(&riffHeader[8], "WAVE", 4) != 0)
        {
            return AudioStatus::InvalidArg;
        }

        bool fmtFound = false;
        dng::u16 fmtTag = 0;
        dng::u16 fmtChannels = 0;
        dng::u32 fmtSampleRate = 0;
        dng::u16 fmtBitsPerSample = 0;

        dng::u64 cursor = 12u;
        while (cursor + 8u <= fileSize)
        {
            dng::u8 chunkHeader[8]{};
            if (!ReadFileRangeExact(fileSystem, path, cursor, chunkHeader, sizeof(chunkHeader), readStatus))
            {
                return (readStatus == fs::FsStatus::Ok)
                    ? AudioStatus::InvalidArg
                    : MapFsStatusToAudioStatus(readStatus);
            }
            cursor += 8u;

            const dng::u32 chunkSize = ReadLe32(&chunkHeader[4]);
            if (static_cast<dng::u64>(chunkSize) > (fileSize - cursor))
            {
                return AudioStatus::InvalidArg;
            }

            const bool hasPadByte = (chunkSize & 1u) != 0u;

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            {
                if (chunkSize < 16u)
                {
                    return AudioStatus::InvalidArg;
                }

                dng::u8 fmtChunk[16]{};
                if (!ReadFileRangeExact(fileSystem, path, cursor, fmtChunk, sizeof(fmtChunk), readStatus))
                {
                    return (readStatus == fs::FsStatus::Ok)
                        ? AudioStatus::InvalidArg
                        : MapFsStatusToAudioStatus(readStatus);
                }

                fmtTag = ReadLe16(&fmtChunk[0]);
                fmtChannels = ReadLe16(&fmtChunk[2]);
                fmtSampleRate = ReadLe32(&fmtChunk[4]);
                fmtBitsPerSample = ReadLe16(&fmtChunk[14]);
                fmtFound = true;
            }
            else if (std::memcmp(chunkHeader, "data", 4) == 0)
            {
                if (!fmtFound ||
                    !IsSupportedPcmFormat(fmtTag, fmtChannels, fmtSampleRate, fmtBitsPerSample) ||
                    (chunkSize % 2u) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                const dng::u64 sampleCount = static_cast<dng::u64>(chunkSize / 2u);
                if (sampleCount == 0u ||
                    (sampleCount % static_cast<dng::u64>(fmtChannels)) != 0u)
                {
                    return AudioStatus::InvalidArg;
                }

                outInfo.channelCount = fmtChannels;
                outInfo.sampleRate = fmtSampleRate;
                outInfo.dataOffsetBytes = cursor;
                outInfo.dataSizeBytes = chunkSize;
                return AudioStatus::Ok;
            }

            const dng::u64 nextCursor = cursor + static_cast<dng::u64>(chunkSize) + (hasPadByte ? 1u : 0u);
            if (nextCursor < cursor || nextCursor > fileSize)
            {
                return AudioStatus::InvalidArg;
            }
            cursor = nextCursor;
        }

        return AudioStatus::InvalidArg;
    }
} // namespace dng::audio::detail
