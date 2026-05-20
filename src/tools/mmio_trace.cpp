#include "tools/mmio_trace.hpp"
#include "memory/bus.hpp"

#include <cstdio>

namespace micro_forge::tools {

void enable_mmio_trace(memory::Bus& bus, MmioTraceSink sink) {
    bus.set_trace([sink = std::move(sink)](const memory::BusTraceEvent& event) {
        MmioAccess access{event.is_write, event.addr,  event.value, event.width,
                          event.ok,       event.error, event.device};
        sink(access);
    });
}

void disable_mmio_trace(memory::Bus& bus) {
    bus.set_trace(nullptr);
}

std::string_view format_mmio_access(const MmioAccess& a, char* buf,
                                    size_t size) {
    const char* op = a.is_write ? "WR" : "RD";
    const char* w = a.width == Width::Word       ? "W"
                    : a.width == Width::HalfWord ? "H"
                                                 : "B";
    const char* status = a.ok ? "OK" : "ERR";
    std::snprintf(buf, size, "[%s] 0x%08X = 0x%08X (%s) %s %.*s", op, a.addr,
                  a.value, w, status, static_cast<int>(a.device.size()),
                  a.device.data());
    return {buf};
}

} // namespace micro_forge::tools
