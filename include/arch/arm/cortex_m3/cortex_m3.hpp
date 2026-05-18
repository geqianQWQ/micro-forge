#pragma once

#include "autogen/arch_details.hpp"
#include "cpu/cpu.hpp"
#include "cpu/regfile.hpp"
#include "def.h"
#include "memory/bus.hpp"
#include "util/weak_ptr/weak_ptr.h"
#include <cstdint>

namespace micro_forge::cpu {
namespace arm::cortex_m3 {
class CortexM3CPU : public CPU {
public:
    explicit CortexM3CPU(WeakPtr<memory::Bus> bus) : bus_(bus) {}
    // CPU Interfaces
    CPUExpected<void> reset() override;
    CPUExpected<void> step() override;
    CPUExpected<State> state() const noexcept override {
        return current_status_;
    }
    CPUExpected<data_t> register_value(std::size_t index) const override;
    CPUExpected<void> set_register_value(std::size_t index,
                                         data_t value) override;
    CPUExpected<std::string_view> register_name(std::size_t idx) const override;
    CPUExpected<std::size_t> register_count() const override { return REGCNT; }
    CPUExpected<addr_t> pc() const override;
    CPUExpected<addr_t> set_pc(addr_t new_pc) override;
    CPUExpected<void> raise_irq(intr::intr_n_t irq_index) override;
    CPUExpected<ticks_t> cycles() const override;

    void launch() noexcept { current_status_ = State::Running; };

    WeakPtr<memory::Bus> memory_bus() { return bus_; }

  private:
    Expected<uint16_t> fetch16(addr_t addr);
    CPUExpected<void> execute_16bit(uint16_t insn);
    CPUExpected<void> execute_32bit(uint16_t hw1, uint16_t hw2);
    CPU::CPUExpected<addr_t> read_pc_raw() const;
    CPU::CPUExpected<void> write_reg(uint8_t index, data_t value);

    /* Algorithm Helpers */
    void update_nz(data_t result);
    enum class FlagPostOperation { Add, Sub };
    void update_flags(FlagPostOperation p, data_t a, data_t b, data_t result);

    /* If we get false, then we need to jump */
    bool condition_need_execute(uint8_t command);
    CPUExpected<void> push_stack(data_t val);
    CPUExpected<data_t> pop_stack();

  private:
    WeakPtr<memory::Bus> bus_;
    reg::Registers<REGCNT> regs_;
    State current_status_ = State::Halted;

    data_t xpsr_ = 0;    // CPU Status Flags as XPSR Register
    data_t primask_ = 0; // Intr Mask Registers
    data_t control_ = 0;
    ticks_t cycles_ = 0;
};
} // namespace arm::cortex_m3
} // namespace micro_forge::cpu
