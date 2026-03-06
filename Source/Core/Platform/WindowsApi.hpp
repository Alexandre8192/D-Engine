// ============================================================================
// D-Engine - Source/Core/Platform/WindowsApi.hpp
// ----------------------------------------------------------------------------
// Purpose : Centralize raw Win32 header inclusion behind the platform layer.
// Contract: Include only from code that already depends on Windows-specific
//           behavior. No runtime logic lives here.
// Notes   : Keeps NOMINMAX and similar preprocessor details out of tools/tests.
// ============================================================================

#pragma once

#include "PlatformDefines.hpp"

#if DNG_PLATFORM_WINDOWS
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif
