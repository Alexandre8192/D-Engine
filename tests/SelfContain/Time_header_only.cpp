#include "Core/Contracts/Time.hpp"
#include "Core/Time/NullTime.hpp"

namespace
{
    using namespace dng::time;

    static_assert(TimeBackend<NullTime>, "NullTime must satisfy the time contract.");

    struct DummyTime
    {
        [[nodiscard]] constexpr TimeCaps GetCaps() const noexcept
        {
            TimeCaps caps{};
            caps.determinism = dng::DeterminismMode::Replay;
            caps.threadSafety = dng::ThreadSafetyMode::ExternalSync;
            caps.stableSampleOrder = true;
            return caps;
        }
        [[nodiscard]] Nanoseconds NowMonotonicNs() noexcept { return 0; }
        void BeginFrame() noexcept {}
        void EndFrame() noexcept {}
    };

    static_assert(TimeBackend<DummyTime>, "DummyTime must satisfy the time contract.");

    void UseTimeInterface() noexcept
    {
        DummyTime backend{};
        auto iface = MakeTimeInterface(backend);
        BeginFrame(iface);
        (void)NowMonotonicNs(iface);
        EndFrame(iface);
        (void)QueryCaps(iface);
    }
}
