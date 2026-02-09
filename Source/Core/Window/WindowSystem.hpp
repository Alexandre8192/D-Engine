// ============================================================================
// D-Engine - Source/Core/Window/WindowSystem.hpp
// ----------------------------------------------------------------------------
// Purpose : High-level window system that owns a backend instance and exposes
//           unified window creation, destruction, event polling, and surface
//           size queries to the rest of the engine.
// Contract: Header-only, no exceptions/RTTI, no allocations in this layer.
//           Lifetime of the backend is tied to WindowSystemState.
//           Thread-safety and determinism follow WindowCaps from the backend;
//           callers must serialize access per instance.
// Notes   : Defaults to the NullWindow backend but accepts external backends
//           via interface injection.
// ============================================================================

#pragma once

#include "Core/Contracts/Window.hpp"
#include "Core/Window/NullWindow.hpp"

namespace dng::win
{
    enum class WindowSystemBackend : dng::u8
    {
        Null,
        External
    };

    struct WindowSystemConfig
    {
        WindowSystemBackend backend = WindowSystemBackend::Null;
    };

    struct WindowSystemState
    {
        WindowInterface       interface{};
        WindowSystemBackend   backend       = WindowSystemBackend::Null;
        NullWindow            nullBackend{};
        bool                  isInitialized = false;
    };

    [[nodiscard]] inline bool InitWindowSystemWithInterface(WindowSystemState& state,
                                                            WindowInterface interface,
                                                            WindowSystemBackend backend) noexcept
    {
        if (interface.userData == nullptr ||
            interface.vtable.getCaps == nullptr ||
            interface.vtable.createWindow == nullptr ||
            interface.vtable.destroyWindow == nullptr ||
            interface.vtable.pollEvents == nullptr ||
            interface.vtable.getSurfaceSize == nullptr)
        {
            return false;
        }

        state.interface     = interface;
        state.backend       = backend;
        state.isInitialized = true;
        return true;
    }

    [[nodiscard]] inline bool InitWindowSystem(WindowSystemState& state,
                                               const WindowSystemConfig& config) noexcept
    {
        state = WindowSystemState{};

        switch (config.backend)
        {
            case WindowSystemBackend::Null:
            {
                WindowInterface iface = MakeNullWindowInterface(state.nullBackend);
                return InitWindowSystemWithInterface(state, iface, WindowSystemBackend::Null);
            }
            case WindowSystemBackend::External:
            {
                return false; // Must be injected via InitWindowSystemWithInterface.
            }
            default:
            {
                return false;
            }
        }
    }

    inline void ShutdownWindowSystem(WindowSystemState& state) noexcept
    {
        state.interface     = WindowInterface{};
        state.backend       = WindowSystemBackend::Null;
        state.nullBackend   = NullWindow{};
        state.isInitialized = false;
    }

    [[nodiscard]] inline WindowStatus CreateWindow(WindowSystemState& state, const WindowDesc& desc, WindowHandle& outHandle) noexcept
    {
        if (!state.isInitialized)
        {
            outHandle = WindowHandle::Invalid();
            return WindowStatus::InvalidArg;
        }
        return CreateWindow(state.interface, desc, outHandle);
    }

    [[nodiscard]] inline WindowCaps QueryCaps(const WindowSystemState& state) noexcept
    {
        return state.isInitialized ? QueryCaps(state.interface) : WindowCaps{};
    }

    [[nodiscard]] inline WindowStatus DestroyWindow(WindowSystemState& state, WindowHandle handle) noexcept
    {
        if (!state.isInitialized)
        {
            return WindowStatus::InvalidArg;
        }
        return DestroyWindow(state.interface, handle);
    }

    [[nodiscard]] inline WindowStatus PollEvents(WindowSystemState& state, WindowEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept
    {
        if (!state.isInitialized)
        {
            outCount = 0;
            return WindowStatus::InvalidArg;
        }
        return PollEvents(state.interface, events, capacity, outCount);
    }

    [[nodiscard]] inline WindowStatus GetSurfaceSize(WindowSystemState& state, WindowHandle handle, dng::u32& outWidth, dng::u32& outHeight) noexcept
    {
        if (!state.isInitialized)
        {
            outWidth  = 0;
            outHeight = 0;
            return WindowStatus::InvalidArg;
        }
        return GetSurfaceSize(state.interface, handle, outWidth, outHeight);
    }

} // namespace dng::win
