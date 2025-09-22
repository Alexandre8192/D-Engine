// =============================
// Timer.hpp
// =============================
#pragma once

// Provides simple time measurement utilities.
// No multithreading or async support yet.

// Intent:
// - Stopwatch-like behavior (Start/Stop/Elapsed)
// - Use for performance testing, animations, DeltaTime estimation
// - Eventually support a global GameClock and DeltaTime manager

// Do NOT add heavy platform-dependent logic here.
// Start with std::chrono if STL is used.