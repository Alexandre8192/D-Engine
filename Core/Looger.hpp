// =============================
// Logger.hpp
// =============================
#pragma once

// This is the simplest logging system for D Engine.
// It should be safe to use everywhere, even in low-level code.

// Intent:
// - Provide basic output to console (Log, Warning, Error)
// - Minimal dependency, no allocation
// - Customizable levels, optional output to file or screen (in later versions)
// - Define log macros like DE_LOG(Log, "Message")

// Do NOT depend on external logging libs.
// Do NOT make this system mandatory in release builds (can be disabled)

// Suggested levels: Info, Warning, Error, Fatal
// Future: optional category/tag support