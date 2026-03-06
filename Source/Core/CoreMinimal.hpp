// ============================================================================
// D-Engine - Source/Core/CoreMinimal.hpp
// ----------------------------------------------------------------------------
// Purpose : Publish the smallest stable umbrella include for files that only
//           need platform/compiler flags, canonical types, and basic macros.
// Contract: No runtime systems, no logging, no timers, and no memory subsystem
//           abstractions are pulled in transitively. This header must remain
//           self-contained and side-effect free.
// Notes   : Use this only when a translation unit genuinely needs the shared
//           low-level baseline. Higher-level facilities such as Logger,
//           allocators, math, and timing must be included explicitly.
// ============================================================================

#pragma once

#include "Core/Platform/PlatformDefines.hpp"
#include "Core/Platform/PlatformCompiler.hpp"
#include "Core/Platform/PlatformMacros.hpp"
#include "Core/Types.hpp"

