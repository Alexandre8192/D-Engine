// ============================================================================
// D-Engine - Source/Core/Contracts/Input.hpp
// ----------------------------------------------------------------------------
// Purpose : Input contract describing backend-agnostic, poll-based event
//           retrieval without exposing platform details.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is delegated to the backend owner.
// Notes   : Minimal shape for keys/buttons; backends may map scan codes or
//           buttons to `InputKey` values.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::input
{
    using InputDeviceId = dng::u32;
    using InputKey      = dng::u32;

    enum class InputEventType : dng::u8
    {
        Unknown = 0,
        Button,
        Axis
    };

    enum class InputStatus : dng::u8
    {
        Ok = 0,
        InvalidArg,
        NotSupported,
        UnknownError
    };

    struct InputEvent
    {
        InputEventType type      = InputEventType::Unknown;
        InputDeviceId  deviceId  = 0;
        InputKey       key       = 0;
        float          valueFloat = 0.0f;
        dng::i32       valueInt   = 0;
    };

    static_assert(std::is_trivially_copyable_v<InputEvent>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    struct InputVTable
    {
        using PollEventsFunc = InputStatus(*)(void* userData, InputEvent* outEvents, dng::u32 capacity, dng::u32& outCount) noexcept;

        PollEventsFunc pollEvents = nullptr;
    };

    struct InputInterface
    {
        InputVTable vtable{};
        void*       userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline InputStatus PollEvents(InputInterface& iface, InputEvent* outEvents, dng::u32 capacity, dng::u32& outCount) noexcept
    {
        outCount = 0;
        return (iface.vtable.pollEvents && iface.userData)
            ? iface.vtable.pollEvents(iface.userData, outEvents, capacity, outCount)
            : InputStatus::InvalidArg;
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept InputBackend = requires(Backend& backend, InputEvent* events, dng::u32 capacity, dng::u32& outCount)
    {
        { backend.PollEvents(events, capacity, outCount) } noexcept -> std::same_as<InputStatus>;
    };

    namespace detail
    {
        template <typename Backend>
        struct InputInterfaceAdapter
        {
            static InputStatus PollEvents(void* userData, InputEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept
            {
                return static_cast<Backend*>(userData)->PollEvents(events, capacity, outCount);
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline InputInterface MakeInputInterface(Backend& backend) noexcept
    {
        static_assert(InputBackend<Backend>, "Backend must satisfy InputBackend concept.");

        InputInterface iface{};
        iface.userData         = &backend;
        iface.vtable.pollEvents = &detail::InputInterfaceAdapter<Backend>::PollEvents;
        return iface;
    }

} // namespace dng::input
