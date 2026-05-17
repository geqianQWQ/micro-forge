#pragma once

#include <cstdio>

// Compile-time log level filter.
// Define MF_LOG_LEVEL before including this header, or pass -DMF_LOG_LEVEL=N
// to the compiler. Messages below this level are compiled out entirely.
//
//   0 = TRACE (default, everything)
//   1 = DEBUG
//   2 = INFO
//   3 = WARN
//   4 = ERROR
//   5 = OFF   (all logging disabled)
#ifndef MF_LOG_LEVEL
#define MF_LOG_LEVEL 0
#endif

#define MF_LVL_TRACE 0
#define MF_LVL_DEBUG 1
#define MF_LVL_INFO  2
#define MF_LVL_WARN  3
#define MF_LVL_ERROR 4
#define MF_LVL_OFF   5

#define MF_LOG_EMIT(level_str, module, msg) \
    do { std::fprintf(stderr, "[%s][%s] %s\n", (level_str), (module), (msg)); } while (0)

#if MF_LOG_LEVEL <= MF_LVL_TRACE
#define LOG_TRACE(module, msg) MF_LOG_EMIT("TRACE", module, msg)
#else
#define LOG_TRACE(module, msg) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_DEBUG
#define LOG_DEBUG(module, msg) MF_LOG_EMIT("DEBUG", module, msg)
#else
#define LOG_DEBUG(module, msg) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_INFO
#define LOG_INFO(module, msg) MF_LOG_EMIT("INFO", module, msg)
#else
#define LOG_INFO(module, msg) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_WARN
#define LOG_WARN(module, msg) MF_LOG_EMIT("WARN", module, msg)
#else
#define LOG_WARN(module, msg) ((void)0)
#endif

#if MF_LOG_LEVEL <= MF_LVL_ERROR
#define LOG_ERROR(module, msg) MF_LOG_EMIT("ERROR", module, msg)
#else
#define LOG_ERROR(module, msg) ((void)0)
#endif
