// ============================================================================
// D-Engine - Source/Core/Contracts/Window.hpp
// ----------------------------------------------------------------------------
// Purpose : Window contract describing backend-agnostic window creation,
//           destruction, event polling, and surface size queries without
//           exposing platform details or performing allocations.
// Contract: Header-only, no exceptions/RTTI, engine-absolute includes only.
//           All types are POD or trivially copyable; no allocations occur in
//           this layer. Thread-safety is delegated to the backend owner.
// Notes   : Title is passed as a non-owning view; no std::string at the edge.
// ============================================================================

#pragma once

#include "Core/Types.hpp"

#include <concepts>
#include <type_traits>

namespace dng::win
{
    struct TitleView
    {
        const char* data = nullptr; // Non-owning, not guaranteed null-terminated.
        dng::u32    size = 0;
    };

    struct WindowHandle
    {
        dng::u32 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] static constexpr WindowHandle Invalid() noexcept { return WindowHandle{}; }
    };

    struct WindowDesc
    {
        dng::u32  width  = 0;
        dng::u32  height = 0;
        TitleView title  {};
    };

    struct WindowCaps
    {
        dng::DeterminismMode determinism = dng::DeterminismMode::Unknown;
        dng::ThreadSafetyMode threadSafety = dng::ThreadSafetyMode::Unknown;
        bool stableEventOrder = false;
    };

    static_assert(std::is_trivially_copyable_v<WindowCaps>);

    enum class WindowEventType : dng::u8
    {
        CloseRequested = 0,
        Resized
    };

    struct WindowEvent
    {
        WindowEventType type = WindowEventType::CloseRequested;
        WindowHandle     handle{};
        dng::u32         width  = 0; // Used when type == Resized.
        dng::u32         height = 0; // Used when type == Resized.
    };

    enum class WindowStatus : dng::u8
    {
        Ok = 0,
        InvalidArg,
        NotSupported,
        UnknownError
    };

    static_assert(std::is_trivially_copyable_v<TitleView>);
    static_assert(std::is_trivially_copyable_v<WindowHandle>);
    static_assert(std::is_trivially_copyable_v<WindowDesc>);
    static_assert(std::is_trivially_copyable_v<WindowEvent>);

    // ------------------------------------------------------------------------
    // Dynamic face (tiny v-table for late binding)
    // ------------------------------------------------------------------------

    struct WindowVTable
    {
        using CreateWindowFunc  = WindowStatus(*)(void* userData, const WindowDesc& desc, WindowHandle& outHandle) noexcept;
        using DestroyWindowFunc = WindowStatus(*)(void* userData, WindowHandle handle) noexcept;
        using PollEventsFunc    = WindowStatus(*)(void* userData, WindowEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept;
        using GetSurfaceSizeFunc= WindowStatus(*)(void* userData, WindowHandle handle, dng::u32& outWidth, dng::u32& outHeight) noexcept;
        using GetCapsFunc        = WindowCaps(*)(const void* userData) noexcept;

        CreateWindowFunc   createWindow   = nullptr;
        DestroyWindowFunc  destroyWindow  = nullptr;
        PollEventsFunc     pollEvents     = nullptr;
        GetSurfaceSizeFunc getSurfaceSize = nullptr;
        GetCapsFunc        getCaps        = nullptr;
    };

    struct WindowInterface
    {
        WindowVTable vtable{};
        void*        userData = nullptr; // Non-owning backend instance pointer.
    };

    [[nodiscard]] inline WindowCaps QueryCaps(const WindowInterface& iface) noexcept
    {
        return (iface.vtable.getCaps && iface.userData)
            ? iface.vtable.getCaps(iface.userData)
            : WindowCaps{};
    }

    [[nodiscard]] inline WindowStatus CreateWindow(WindowInterface& iface, const WindowDesc& desc, WindowHandle& outHandle) noexcept
    {
        outHandle = WindowHandle::Invalid();
        return (iface.vtable.createWindow && iface.userData)
            ? iface.vtable.createWindow(iface.userData, desc, outHandle)
            : WindowStatus::InvalidArg;
    }

    [[nodiscard]] inline WindowStatus DestroyWindow(WindowInterface& iface, WindowHandle handle) noexcept
    {
        return (iface.vtable.destroyWindow && iface.userData)
            ? iface.vtable.destroyWindow(iface.userData, handle)
            : WindowStatus::InvalidArg;
    }

    [[nodiscard]] inline WindowStatus PollEvents(WindowInterface& iface, WindowEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept
    {
        outCount = 0;
        return (iface.vtable.pollEvents && iface.userData)
            ? iface.vtable.pollEvents(iface.userData, events, capacity, outCount)
            : WindowStatus::InvalidArg;
    }

    [[nodiscard]] inline WindowStatus GetSurfaceSize(WindowInterface& iface, WindowHandle handle, dng::u32& outWidth, dng::u32& outHeight) noexcept
    {
        outWidth  = 0;
        outHeight = 0;
        return (iface.vtable.getSurfaceSize && iface.userData)
            ? iface.vtable.getSurfaceSize(iface.userData, handle, outWidth, outHeight)
            : WindowStatus::InvalidArg;
    }

    // ------------------------------------------------------------------------
    // Static face (concept + adapter to dynamic v-table)
    // ------------------------------------------------------------------------

    template <typename Backend>
    concept WindowBackend = requires(Backend& backend,
                                     const Backend& constBackend,
                                     const WindowDesc& desc,
                                     WindowHandle handle,
                                     WindowEvent* events,
                                     dng::u32 capacity,
                                     dng::u32& outCount,
                                     dng::u32& outWidth,
                                     dng::u32& outHeight)
    {
        { constBackend.GetCaps() } noexcept -> std::same_as<WindowCaps>;
        { backend.CreateWindow(desc, handle) } noexcept -> std::same_as<WindowStatus>;
        { backend.DestroyWindow(handle) } noexcept -> std::same_as<WindowStatus>;
        { backend.PollEvents(events, capacity, outCount) } noexcept -> std::same_as<WindowStatus>;
        { backend.GetSurfaceSize(handle, outWidth, outHeight) } noexcept -> std::same_as<WindowStatus>;
    };

    namespace detail
    {
        template <typename Backend>
        struct WindowInterfaceAdapter
        {
            static WindowCaps GetCaps(const void* userData) noexcept
            {
                return static_cast<const Backend*>(userData)->GetCaps();
            }

            static WindowStatus CreateWindow(void* userData, const WindowDesc& desc, WindowHandle& outHandle) noexcept
            {
                return static_cast<Backend*>(userData)->CreateWindow(desc, outHandle);
            }

            static WindowStatus DestroyWindow(void* userData, WindowHandle handle) noexcept
            {
                return static_cast<Backend*>(userData)->DestroyWindow(handle);
            }

            static WindowStatus PollEvents(void* userData, WindowEvent* events, dng::u32 capacity, dng::u32& outCount) noexcept
            {
                return static_cast<Backend*>(userData)->PollEvents(events, capacity, outCount);
            }

            static WindowStatus GetSurfaceSize(void* userData, WindowHandle handle, dng::u32& outWidth, dng::u32& outHeight) noexcept
            {
                return static_cast<Backend*>(userData)->GetSurfaceSize(handle, outWidth, outHeight);
            }
        };
    } // namespace detail

    template <typename Backend>
    [[nodiscard]] inline WindowInterface MakeWindowInterface(Backend& backend) noexcept
    {
        static_assert(WindowBackend<Backend>, "Backend must satisfy WindowBackend concept.");

        WindowInterface iface{};
        iface.userData              = &backend;
        iface.vtable.getCaps        = &detail::WindowInterfaceAdapter<Backend>::GetCaps;
        iface.vtable.createWindow   = &detail::WindowInterfaceAdapter<Backend>::CreateWindow;
        iface.vtable.destroyWindow  = &detail::WindowInterfaceAdapter<Backend>::DestroyWindow;
        iface.vtable.pollEvents     = &detail::WindowInterfaceAdapter<Backend>::PollEvents;
        iface.vtable.getSurfaceSize = &detail::WindowInterfaceAdapter<Backend>::GetSurfaceSize;
        return iface;
    }

} // namespace dng::win
