#include "util/logger.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace micro_forge::util {
namespace {

void stderr_sink(LogLevel level, std::string_view module,
                 std::string_view message) {
    std::fprintf(stderr, "[%.*s][%.*s] %.*s\n",
                 static_cast<int>(log_level_name(level).size()),
                 log_level_name(level).data(), static_cast<int>(module.size()),
                 module.data(), static_cast<int>(message.size()),
                 message.data());
}

LogSink& active_sink() {
    static LogSink sink = stderr_sink;
    return sink;
}

std::string vformat(const char* fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len <= 0) {
        return {};
    }

    std::vector<char> buf(static_cast<std::size_t>(len) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);
    std::string out(buf.data(), static_cast<std::size_t>(len));
    return out;
}

} // namespace

std::string_view log_level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Off:
            return "OFF";
    }
    return "UNKNOWN";
}

void set_log_sink(LogSink sink) {
    active_sink() = std::move(sink);
}

void reset_log_sink() {
    active_sink() = stderr_sink;
}

void log_message(LogLevel level, std::string_view module, const char* fmt,
                 ...) {
    if (level == LogLevel::Off || !active_sink()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    auto message = vformat(fmt, args);
    va_end(args);

    active_sink()(level, module, message);
}

} // namespace micro_forge::util
