#pragma once

#include "autogen/arch_details.hpp"
#include "cpu/cpu.hpp"
#include "cpu/regfile.hpp"
#include "def.h"
#include "memory/bus.hpp"
#include "periph/nvic.hpp"
#include "util/weak_ptr/weak_ptr.h"
#include "util/weak_ptr/weak_ptr_factory.h"
#include <cstdint>
#include <tuple>
#include <vector>

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

    void set_nvic(periph::NvicPeripheral& nvic) { nvic_ = &nvic; }
    bool in_handler_mode() const { return in_handler_mode_; }

    // Probe mode: skip illegal instructions and log opcodes instead of halting
    void enable_probe_mode(bool on = true) { probe_mode_ = on; }
    const auto& missing_opcodes() const { return missing_opcodes_; }
    void clear_missing_opcodes() { missing_opcodes_.clear(); }

    WeakPtr<CortexM3CPU> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    Expected<uint16_t> fetch16(addr_t addr);
    CPUExpected<void> execute_16bit(uint16_t insn);
    CPUExpected<void> execute_32bit(uint16_t hw1, uint16_t hw2);
    CPU::CPUExpected<addr_t> read_pc_raw() const;
    CPU::CPUExpected<void> write_reg(uint8_t index, data_t value);

    // Unified PC write — detects EXC_RETURN in handler mode
    CPUExpected<void> write_pc(data_t value);

    /* Algorithm Helpers */
    void update_nz(data_t result);
    enum class FlagPostOperation { Add, Sub };
    void update_flags(FlagPostOperation p, data_t a, data_t b, data_t result);

    /* If we get false, then we need to jump */
    bool condition_need_execute(uint8_t command);
    CPUExpected<void> push_stack(data_t val);
    CPUExpected<data_t> pop_stack();

    // Interrupt handling
    CPUExpected<void> check_and_handle_interrupt();
    CPUExpected<void> interrupt_entry(uint8_t irq_n);
    CPUExpected<void> interrupt_return(data_t exc_return);
    CPUExpected<void> trigger_hardfault();

  private:
    WeakPtr<memory::Bus> bus_;
    reg::Registers<REGCNT> regs_;
    State current_status_ = State::Halted;

    data_t xpsr_ = 0;    // CPU Status Flags as XPSR Register
    data_t primask_ = 0; // Intr Mask Registers
    data_t control_ = 0;
    ticks_t cycles_ = 0;

    // Interrupt state
    periph::NvicPeripheral* nvic_ = nullptr;
    bool in_handler_mode_ = false;
    uint8_t current_priority_ = 0xFF;
    addr_t vector_table_base_ = 0x08000000;

    // Probe mode state
    bool probe_mode_ = false;
    std::vector<std::tuple<addr_t, uint16_t, uint16_t>> missing_opcodes_;

    WeakPtrFactory<CortexM3CPU> weak_factory_{this};
};
} // namespace arm::cortex_m3
} // namespace micro_forge::cpu
