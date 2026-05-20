#include "tools/memory_dump.hpp"
#include "memory/bus.hpp"
#include "util/logger.hpp"

#include <cstdio>
#include <string>

namespace micro_forge::tools {

void memory_dump(memory::Bus& bus, addr_t addr, addr_t size,
                 std::function<void(std::string_view)> sink) {
    constexpr addr_t bytes_per_line = 16;

    for (addr_t row = 0; row < size; row += bytes_per_line) {
        addr_t line_addr = addr + row;
        addr_t line_len = std::min(bytes_per_line, size - row);

        std::string line;
        char buf[16];

        // Address
        std::snprintf(buf, sizeof(buf), "%08X: ", line_addr);
        line += buf;

        // Hex bytes
        uint8_t bytes[16] = {};
        for (addr_t i = 0; i < line_len; ++i) {
            auto r = bus.read(line_addr + i, Width::Byte);
            bytes[i] = r.has_value() ? static_cast<uint8_t>(*r) : 0xFF;
            std::snprintf(buf, sizeof(buf), "%02X ", bytes[i]);
            line += buf;
        }
        // Padding for short lines
        for (addr_t i = line_len; i < bytes_per_line; ++i) {
            line += "   ";
        }

        line += "|";

        // ASCII
        for (addr_t i = 0; i < line_len; ++i) {
            char c = (bytes[i] >= 0x20 && bytes[i] <= 0x7E)
                     ? static_cast<char>(bytes[i]) : '.';
            line += c;
        }

        line += "|";
        sink(line);
    }
}

void memory_dump(memory::Bus& bus, addr_t addr, addr_t size) {
    memory_dump(bus, addr, size, [](std::string_view line) {
        LOG_INFO("memory_dump", "%.*s", static_cast<int>(line.size()),
                 line.data());
    });
}

} // namespace micro_forge::tools
