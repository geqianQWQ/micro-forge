#pragma once

#include <functional>
#include <string_view>

// Compile-time log level filter.
// Define MF_LOG_LEVEL before including this header, or pass -DMF_LOG_LEVEL=N
// to the compiler. Messages below this level are compiled out entirely.
//
//   0 = TRACE
//   1 = DEBUG
//   2 = INFO (default)
//   3 = WARN
//   4 = ERROR
//   5 = OFF   (all logging disabled)
#ifndef MF_LOG_LEVEL
#define MF_LOG_LEVEL 2
#endif

#define MF_LVL_TRACE 0
#define MF_LVL_DEBUG 1
#define MF_LVL_INFO  2
#define MF_LVL_WARN  3
#define MF_LVL_ERROR 4
#define MF_LVL_OFF   5

namespace micro_forge::util {

enum class LogLevel {
    Trace = MF_LVL_TRACE,
    Debug = MF_LVL_DEBUG,
    Info = MF_LVL_INFO,
    Warn = MF_LVL_WARN,
    Error = MF_LVL_ERROR,
    Off = MF_LVL_OFF,
};

using LogSink = std::function<void(LogLevel level, std::string_view module,
                                   std::string_view message)>;

std::string_view log_level_name(LogLevel level) noexcept;
void set_log_sink(LogSink sink);
void reset_log_sink();
void log_message(LogLevel level, std::string_view module, const char* fmt, ...);

} // namespace micro_forge::util

#define MF_LOG_EMIT(level, module, fmt, ...)                                      \
    do {                                                                          \
        ::micro_forge::util::log_message((level), (module), (fmt)                 \
                                         __VA_OPT__(, ) __VA_ARGS__);             \
    } while (0)

#if MF_LOG_LEVEL <= MF_LVL_TRACE
#define LOG_TRACE(module, fmt, ...)                                               \
    MF_LOG_EMIT(::micro_forge::util::LogLevel::Trace, module, fmt                 \
                __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_TRACE(module, fmt, ...) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_DEBUG
#define LOG_DEBUG(module, fmt, ...)                                               \
    MF_LOG_EMIT(::micro_forge::util::LogLevel::Debug, module, fmt                 \
                __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_DEBUG(module, fmt, ...) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_INFO
#define LOG_INFO(module, fmt, ...)                                                \
    MF_LOG_EMIT(::micro_forge::util::LogLevel::Info, module, fmt                  \
                __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_INFO(module, fmt, ...) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_WARN
#define LOG_WARN(module, fmt, ...)                                                \
    MF_LOG_EMIT(::micro_forge::util::LogLevel::Warn, module, fmt                  \
                __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_WARN(module, fmt, ...) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_ERROR
#define LOG_ERROR(module, fmt, ...)                                               \
    MF_LOG_EMIT(::micro_forge::util::LogLevel::Error, module, fmt                 \
                __VA_OPT__(, ) __VA_ARGS__)
#else
#define LOG_ERROR(module, fmt, ...) ((void)0)
#endif
