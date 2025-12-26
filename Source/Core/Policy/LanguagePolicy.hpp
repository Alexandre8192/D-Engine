#pragma once

// ============================================================================
// D-Engine - Source/Core/Policy/LanguagePolicy.hpp
// ----------------------------------------------------------------------------
// Purpose : Enforce global language/toolchain rules for D-Engine.
// Contract: Must be force-included by every translation unit (/FI or -include).
// Notes   : Intentionally fails the build when rules are violated.
// ============================================================================

#ifndef DNG_CORE_POLICY_LANGUAGE_POLICY_HPP
#define DNG_CORE_POLICY_LANGUAGE_POLICY_HPP

#define DNG_LANGUAGE_POLICY_FORCE_INCLUDED 1

// Exceptions are forbidden across the entire D-Engine codebase.
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    #error "D-Engine forbids C++ exceptions (try/catch/throw). Disable EH in the toolchain."
#endif

// RTTI is forbidden across the entire D-Engine codebase.
#if defined(_CPPRTTI)
    #error "D-Engine forbids RTTI (dynamic_cast/typeid). Disable RTTI (/GR-)."
#endif

#endif // DNG_CORE_POLICY_LANGUAGE_POLICY_HPP
