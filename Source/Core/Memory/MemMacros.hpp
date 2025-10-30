#pragma once
// ============================================================================
// D-Engine - Core/Memory/MemMacros.hpp
// ----------------------------------------------------------------------------
// Purpose : Centralize memory subsystem compile-time feature gates so build
//           configurations can reason about enabled capabilities from a single
//           header.
// Contract: Header-only and safe to include from any translation unit. All
//           macros default to disabled (0) unless explicitly overridden before
//           this header is included. Validation guards ensure values remain in
//           the documented range.
// Notes   : Future memory subsystem toggles should be added here to avoid
//           scattering feature flags across multiple headers.
// ============================================================================

#ifndef DNG_SMALLOBJ_TLS_BINS
#    define DNG_SMALLOBJ_TLS_BINS 0
#endif

#if (DNG_SMALLOBJ_TLS_BINS != 0) && (DNG_SMALLOBJ_TLS_BINS != 1)
#    error "DNG_SMALLOBJ_TLS_BINS must be 0 (disabled) or 1 (enabled)."
#endif
