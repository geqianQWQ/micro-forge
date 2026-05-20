#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace micro_forge {

namespace memory {
class Bus;
}

namespace cpu::arm::cortex_m3 {

class CortexM3CPU;

std::expected<void, std::string>
cortex_m3_reset(CortexM3CPU& cpu, memory::Bus& bus,
                uint32_t vector_table_base = 0x00000000);

} // namespace cpu::arm::cortex_m3
} // namespace micro_forge
