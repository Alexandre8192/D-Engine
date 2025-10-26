#pragma once
// ============================================================================
// D-Engine - Core/Memory/GlobalNewDelete.hpp
// ----------------------------------------------------------------------------
// Purpose : Publish compile-time switches that control whether the engine
//           overrides the global new/delete operators and, when enabled, how
//           allocations are routed between default and small-object allocators.
// Contract: Header-only configuration shim; safe to include from any TU. The
//           actual operator definitions live in GlobalNewDelete.cpp to preserve
//           the single ODR point mandated by the C++ standard.
// Notes   : Clients may override the macros below prior to including this
//           header. Keeping this file lightweight avoids pulling the full
//           memory system into every translation unit that inspects the gates.
// ============================================================================

// -- Feature gate -----------------------------------------------------------
// DNG_ROUTE_GLOBAL_NEW
// Purpose : Enable routing of global ::operator new/delete through the engine
//           allocators so external code automatically benefits from tracking.
// Contract: When 0 (default) the engine leaves the toolchain's operators
//           untouched. Define to 1 project-wide and compile exactly one TU that defines the operators (GlobalNewDelete.cpp).
//           Mixing TUs compiled with different values is undefined.
#ifndef DNG_ROUTE_GLOBAL_NEW
#define DNG_ROUTE_GLOBAL_NEW 0
#endif

// -- Small allocation threshold --------------------------------------------
// DNG_GLOBAL_NEW_SMALL_THRESHOLD
// Purpose : Decide which allocations are serviced by the small-object allocator
//           versus the default allocator when global routing is enabled.
// Contract: Expressed in bytes; values <= 0 disable the small-object fast path.
#ifndef DNG_GLOBAL_NEW_SMALL_THRESHOLD
#define DNG_GLOBAL_NEW_SMALL_THRESHOLD 1024
#endif

// -- Fallback behaviour -----------------------------------------------------
// DNG_GLOBAL_NEW_FALLBACK_MALLOC
// Purpose : Control whether global operators fall back to std::malloc/free when
//           invoked before MemorySystem::Init().
// Contract: When set to 1 (default) the operators will log a warning once and
//           service requests via the C runtime until the memory system comes
//           online. When set to 0 the operators enforce initialization by
//           triggering the engine's OOM policy if invoked too early.
#ifndef DNG_GLOBAL_NEW_FALLBACK_MALLOC
#define DNG_GLOBAL_NEW_FALLBACK_MALLOC 1
#endif

// Forward declaration left intentionally light to avoid pulling MemorySystem
// into innocuous translation units. GlobalNewDelete.cpp includes the full
// definition.
namespace dng
{
namespace memory
{
    struct MemorySystem;
}
}

// ============================================================================
// Linking / ODR notes
// ----------------------------------------------------------------------------
// * The definition of the global operators resides in
//     Core/Memory/GlobalNewDelete.cpp.
// * Only that translation unit must be compiled when DNG_ROUTE_GLOBAL_NEW=1 to
//   avoid multiple definition errors.
// * Projects that merely want to inspect the configuration should include this
//   header only; they must not provide their own operator definitions.
// ============================================================================
