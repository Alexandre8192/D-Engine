// ============================================================================
// D-Engine - tests/SelfContain/Audio_header_only.cpp
// ----------------------------------------------------------------------------
// Purpose : Verify that Audio.hpp can be included alone without missing deps.
// ============================================================================

#include "Core/Contracts/Audio.hpp"

// Minimal concept check to ensure types are visible
static_assert(std::is_trivially_copyable_v<dng::audio::SoundHandle>);
static_assert(std::is_trivially_copyable_v<dng::audio::VoiceHandle>);
static_assert(std::is_trivially_copyable_v<dng::audio::AudioCaps>);
static_assert(std::is_trivially_copyable_v<dng::audio::PlayParams>);
static_assert(std::is_trivially_copyable_v<dng::audio::AudioInterface>);
