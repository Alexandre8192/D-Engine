#pragma once


// =============================
// D-Engine - Logger.hpp (C++23, minimal but solid)
// =============================
// Goals
//  - Always-safe to include (no heavy deps, header-only)
//  - C++23 std::print/std::println backend (no third-party)
//  - Zero/low overhead when disabled (compile-time switches)
//  - Simple runtime min-level filter and optional category filter
//  - Thread-safe emission (coarse-grained mutex around a single print)
//  - No dynamic allocations in our code (std::print may allocate internally)
//
// Non-goals (for now)
//  - Async logging, ring buffers, files, colors, sinks fan-out
//  - Structured logs (JSON) or source-location rich metadata
//
// Notes
//  - Categories are plain string literals (const char*). Keep them short (e.g., "Memory", "Core").
//  - You can later extend this to route to files or custom sinks while keeping the same macros.

#include <cstdint>
#include <atomic>
#include <mutex>
#include <string_view>
#include <print>        // C++23 std::print / std::println
#include <format>       // std::format_string
#include <cstdio>       // std::FILE, stdout/stderr
#include <cstdlib>      // std::abort

#ifndef DNG_ENABLE_LOGGING
#  define DNG_ENABLE_LOGGING 1
#endif
#ifndef DNG_ENABLE_LOG_ASSERT
#  define DNG_ENABLE_LOG_ASSERT 1
#endif

namespace dng::core {

    enum class LogLevel : std::uint8_t {
        Disabled = 0,
        Fatal = 1,
        Error = 2,
        Warn = 3,
        Info = 4,
        Verbose = 5,
        // NOTE: higher number == more chatty
        // MinLevel policy: a message is emitted if (level <= MinLevel).
    };

    struct LoggerConfig {
        std::atomic<LogLevel> MinLevel{ LogLevel::Info };
        // If non-null, only messages whose category equals this filter are printed.
        // Keep nullptr to accept all categories.
        // Must stay a stable C-string literal (e.g., "Memory").
        std::atomic<const char*> CategoryEqualsFilter{ nullptr };
        // Emit timestamps / thread ids later if needed.
    };

    class Logger final {
    public:
        static Logger& Get() noexcept {
            static Logger g;
            return g;
        }

        static void SetMinLevel(LogLevel lvl) noexcept { Get().mCfg.MinLevel.store(lvl, std::memory_order_relaxed); }
        static LogLevel GetMinLevel() noexcept { return Get().mCfg.MinLevel.load(std::memory_order_relaxed); }
        static void SetCategoryEqualsFilter(const char* cat) noexcept {
            Get().mCfg.CategoryEqualsFilter.store(cat, std::memory_order_relaxed);
        }

        // Public check to short-circuit expensive logging
        static bool IsEnabled(LogLevel lvl, const char* category) noexcept {
            return ShouldEmit(lvl, category);
        }

        // ----------------------
        // Level-specific helpers
        // ----------------------
        template <class... Args>
        static void Info(const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            Print(LogLevel::Info, category, stdout, fmt, static_cast<Args&&>(args)...);
        }
        template <class... Args>
        static void Warn(const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            Print(LogLevel::Warn, category, stderr, fmt, static_cast<Args&&>(args)...);
        }
        template <class... Args>
        static void Error(const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            Print(LogLevel::Error, category, stderr, fmt, static_cast<Args&&>(args)...);
        }
        template <class... Args>
        [[noreturn]] static void Fatal(const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            Print(LogLevel::Fatal, category, stderr, fmt, static_cast<Args&&>(args)...);
            std::fflush(stderr);
            std::abort();
        }
        template <class... Args>
        static void Verbose(const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            Print(LogLevel::Verbose, category, stdout, fmt, static_cast<Args&&>(args)...);
        }

        // Raw message overloads (no fmt)
        static void Info(const char* category, std::string_view msg) noexcept { PrintRaw(LogLevel::Info, category, stdout, msg); }
        static void Warn(const char* category, std::string_view msg) noexcept { PrintRaw(LogLevel::Warn, category, stderr, msg); }
        static void Error(const char* category, std::string_view msg) noexcept { PrintRaw(LogLevel::Error, category, stderr, msg); }
        [[noreturn]] static void Fatal(const char* category, std::string_view msg) noexcept {
            PrintRaw(LogLevel::Fatal, category, stderr, msg);
            std::fflush(stderr);
            std::abort();
        }
        static void Verbose(const char* category, std::string_view msg) noexcept { PrintRaw(LogLevel::Verbose, category, stdout, msg); }

        // Generic entry (level chosen by caller).
        template <class... Args>
        static void Log(LogLevel lvl, const char* category, std::format_string<Args...> fmt, Args&&... args) noexcept {
            std::FILE* stream = (lvl <= LogLevel::Error) ? stderr : stdout;
            Print(lvl, category, stream, fmt, static_cast<Args&&>(args)...);
        }

    private:
        Logger() = default;

        static bool ShouldEmit(LogLevel lvl, const char* category) noexcept {
            Logger& self = Get();
            if (lvl == LogLevel::Disabled) return false;
            if (lvl > self.mCfg.MinLevel.load(std::memory_order_relaxed)) return false;
            const char* filter = self.mCfg.CategoryEqualsFilter.load(std::memory_order_relaxed);
            if (filter) {
                if (!category) return false; // filter active => category is required
                if (std::string_view(filter) != category) return false;
            }
            return true;
        }

        template <class... Args>
        static void Print(LogLevel lvl, const char* category, std::FILE* stream, std::format_string<Args...> fmt, Args&&... args) noexcept {
            if (!ShouldEmit(lvl, category)) return;
            Logger& self = Get();
            const char* lvlStr = ToShortLevel(lvl);
            std::scoped_lock lock(self.mMutex);
            try {
                if (category) {
                    std::print(stream, "[{}][{}] ", lvlStr, category);
                }
                else {
                    std::print(stream, "[{}] ", lvlStr);
                }
                std::println(stream, fmt, static_cast<Args&&>(args)...);
            }
            catch (...) {
                // swallow
            }
        }

        static void PrintRaw(LogLevel lvl, const char* category, std::FILE* stream, std::string_view msg) noexcept {
            if (!ShouldEmit(lvl, category)) return;
            Logger& self = Get();
            const char* lvlStr = ToShortLevel(lvl);
            std::scoped_lock lock(self.mMutex);
            try {
                if (category) {
                    std::println(stream, "[{}][{}] {}", lvlStr, category, msg);
                }
                else {
                    std::println(stream, "[{}] {}", lvlStr, msg);
                }
            }
            catch (...) {
                // swallow
            }
        }

        static const char* ToShortLevel(LogLevel lvl) noexcept {
            switch (lvl) {
            case LogLevel::Fatal:   return "F";
            case LogLevel::Error:   return "E";
            case LogLevel::Warn:    return "W";
            case LogLevel::Info:    return "I";
            case LogLevel::Verbose: return "V";
            default:                return "-";
            }
        }

    private:
        std::mutex mMutex{};
        LoggerConfig mCfg{};
    };

} // namespace dng::core

// ----------------------
// Public log macros (single evaluation of Category)
// ----------------------
#if DNG_ENABLE_LOGGING
#define DNG_LOG_VERBOSE(Category, Fmt, ...) do { \
        const char* _cat = (Category); \
        if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Verbose, _cat)) { \
            ::dng::core::Logger::Verbose(_cat, (Fmt) __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while (0)

#define DNG_LOG_INFO(Category, Fmt, ...) do { \
        const char* _cat = (Category); \
        if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Info, _cat)) { \
            ::dng::core::Logger::Info(_cat, (Fmt) __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while (0)

#define DNG_LOG_WARNING(Category, Fmt, ...) do { \
        const char* _cat = (Category); \
        if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Warn, _cat)) { \
            ::dng::core::Logger::Warn(_cat, (Fmt) __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while (0)

#define DNG_LOG_ERROR(Category, Fmt, ...) do { \
        const char* _cat = (Category); \
        if (::dng::core::Logger::IsEnabled(::dng::core::LogLevel::Error, _cat)) { \
            ::dng::core::Logger::Error(_cat, (Fmt) __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while (0)

#define DNG_LOG_FATAL(Category, Fmt, ...) do { \
        const char* _cat = (Category); \
        ::dng::core::Logger::Fatal(_cat, (Fmt) __VA_OPT__(,) __VA_ARGS__); \
    } while (0)
#else
#define DNG_LOG_VERBOSE(Category, Fmt, ...)  ((void)0)
#define DNG_LOG_INFO(Category, Fmt, ...)     ((void)0)
#define DNG_LOG_WARNING(Category, Fmt, ...)  ((void)0)
#define DNG_LOG_ERROR(Category, Fmt, ...)    ((void)0)
#define DNG_LOG_FATAL(Category, Fmt, ...)    ((void)0)
#endif

// ----------------------
// Assert macro
// ----------------------
#if DNG_ENABLE_LOG_ASSERT
#ifndef DNG_ASSERT
#include <source_location>

// Argument-counting helper to choose ASSERT_1 or ASSERT_2
#define DNG_EXPAND(x) x
#define DNG_GET_MACRO(_1,_2,NAME,...) NAME

// 1-arg form: DNG_ASSERT(Expr)
#define DNG_ASSERT_1(Expr) do { \
            if (!(Expr)) { \
                const auto loc = std::source_location::current(); \
                ::dng::core::Logger::Error("Assert", "{} ({}:{}): assertion failed: {}", \
                    loc.function_name(), loc.file_name(), loc.line(), #Expr); \
            } \
        } while(0)

// 2-arg form: DNG_ASSERT(Expr, Msg)
#define DNG_ASSERT_2(Expr, Msg) do { \
            if (!(Expr)) { \
                const auto loc = std::source_location::current(); \
                ::dng::core::Logger::Error("Assert", "{} ({}:{}): {}", \
                    loc.function_name(), loc.file_name(), loc.line(), Msg); \
            } \
        } while(0)

// Dispatcher that supports both 1-arg and 2-arg calls
#define DNG_ASSERT(...) \
            DNG_EXPAND(DNG_GET_MACRO(__VA_ARGS__, DNG_ASSERT_2, DNG_ASSERT_1)(__VA_ARGS__))

#endif
#else
#ifndef DNG_ASSERT
#define DNG_ASSERT(...) ((void)0)
#endif
#endif
