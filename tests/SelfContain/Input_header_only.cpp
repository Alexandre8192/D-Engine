#include "Core/Contracts/Input.hpp"
#include "Core/Input/NullInput.hpp"

namespace
{
    using namespace dng::input;

    static_assert(InputBackend<NullInput>, "NullInput must satisfy input backend concept.");

    struct DummyInput
    {
        [[nodiscard]] InputStatus PollEvents(InputEvent*, dng::u32, dng::u32& outCount) noexcept
        {
            outCount = 0;
            return InputStatus::Ok;
        }
    };

    static_assert(InputBackend<DummyInput>, "DummyInput must satisfy input backend concept.");

    void UseInputInterface() noexcept
    {
        DummyInput backend{};
        auto iface = MakeInputInterface(backend);
        InputEvent events[2]{};
        dng::u32 count = 0;
        (void)PollEvents(iface, events, 2, count);
    }
}
