#include "arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "memory/bus.hpp"
#include "util/logger.hpp"

namespace micro_forge::cpu::arm::cortex_m3 {

std::expected<void, std::string>
cortex_m3_reset(CortexM3CPU& cpu, memory::Bus& bus,
                uint32_t vector_table_base) {

    auto sp_result = bus.read(vector_table_base + 0, Width::Word);
    if (!sp_result) {
        return std::unexpected("failed to read initial SP from vector table");
    }
    uint32_t sp = *sp_result;

    auto pc_result = bus.read(vector_table_base + 4, Width::Word);
    if (!pc_result) {
        return std::unexpected("failed to read reset handler from vector table");
    }
    uint32_t pc = *pc_result;

    auto reset_r = cpu.reset();
    if (!reset_r) {
        return std::unexpected("CPU reset failed");
    }

    if (!cpu.set_register_value(13, sp)) {
        return std::unexpected("failed to set SP");
    }

    if (!(pc & 1)) {
        LOG_WARN("cpu/reset", "PC bit[0] is 0, forcing Thumb bit");
        pc |= 1;
    }
    // Strip Thumb bit before writing to PC register.
    // Thumb mode is indicated by XPSR.T (set by cpu.reset()).
    uint32_t exec_addr = pc & ~1u;
    if (!cpu.set_pc(exec_addr)) {
        return std::unexpected("failed to set PC");
    }

    if (!cpu.set_register_value(14, 0xFFFFFFFF)) {
        return std::unexpected("failed to set LR");
    }

    LOG_DEBUG("cpu/reset", "SP=0x%08X PC=0x%08X", sp, exec_addr);
    return {};
}

} // namespace micro_forge::cpu::arm::cortex_m3
