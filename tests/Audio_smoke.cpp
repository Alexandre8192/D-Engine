#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/NullAudio.hpp"

int RunAudioSmoke()
{
    using namespace dng::audio;

    AudioSystemState state{};
    NullAudio        backend{};
    AudioInterface   iface = MakeNullAudioInterface(backend);

    if (!InitAudioSystem(state, iface))
    {
        return 1;
    }

    if (!state.caps.deterministic)
    {
        return 2;
    }

    const bool backendHasBuses = state.caps.has_buses || (iface.vtable.setBusVolume != nullptr);
    if (!backendHasBuses)
    {
        return 3;
    }

    SysSetBusVolume(state, AudioBus::Music, 0.5f);
    if (state.busVolumes[static_cast<dng::u32>(AudioBus::Music)] != 0.5f)
    {
        return 4;
    }
    if (backend.GetBusVolume(AudioBus::Music) != 0.5f)
    {
        return 5;
    }

    SysSetMasterVolume(state, 0.25f);
    if (state.busVolumes[static_cast<dng::u32>(AudioBus::Master)] != 0.25f)
    {
        return 6;
    }
    if (backend.GetBusVolume(AudioBus::Master) != 0.25f)
    {
        return 7;
    }

    PlayParams params{};
    params.bus = AudioBus::Sfx;

    VoiceHandle v0 = SysPlay(state, SoundHandle{1}, params);
    VoiceHandle v1 = SysPlay(state, SoundHandle{1}, params);

    if (v0.value != 1 || v1.value != 2)
    {
        return 8;
    }

    SysStop(state, v0);
    SysStop(state, v1);

    if (backend.stats.totalPlays != 2 || backend.stats.totalStops != 2 || backend.stats.activeVoices != 0)
    {
        return 9;
    }

    ShutdownAudioSystem(state);
    return 0;
}
