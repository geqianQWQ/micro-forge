#include "arch/arm/cortex_m3/cortex_m3.hpp"

#include <expected>

namespace micro_forge::cpu::arm::cortex_m3 {

// ── Interrupt handling ──

CPU::CPUExpected<void> CortexM3CPU::write_pc(data_t value) {
    if (in_handler_mode_ && (value & 0xFFFFFFF0u) == 0xFFFFFFF0u) {
        if (value == 0xFFFFFFF1u || value == 0xFFFFFFF9u ||
            value == 0xFFFFFFFDu) {
            return interrupt_return(value);
        }
    }
    return write_reg(15, value & ~1u);
}

CPU::CPUExpected<void> CortexM3CPU::check_and_handle_interrupt() {
    if (in_handler_mode_) {
        return {};
    }
    if (primask_ & 1) {
        return {};
    }
    if (faultmask_ & 1) {
        return {};
    }

    // SysTick is a system exception (vector index 15), bypasses NVIC
    if (pending_sys_tick_) {
        pending_sys_tick_ = false;
        return interrupt_entry_system(15);
    }

    if (!nvic_) {
        return {};
    }
    if (!nvic_->has_pending_irq()) {
        return {};
    }

    uint8_t irq_n = nvic_->highest_pending_irq();
    if (!nvic_->is_enabled(irq_n)) {
        return {};
    }
    if (basepri_ != 0 && nvic_->irq_priority(irq_n) >= basepri_) {
        return {};
    }

    return interrupt_entry(irq_n);
}

CPU::CPUExpected<void> CortexM3CPU::exception_entry_common(addr_t vector_addr) {
    auto push_one = [&](data_t val) -> CPUExpected<void> {
        auto r = push_stack(val);
        if (!r) {
            return std::unexpected{CPUError::ExceptionEntryFault};
        }
        return {};
    };

    auto res = push_one(xpsr_);
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(15).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(14).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(12).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(3).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(2).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(1).value_or(0));
    if (!res) {
        return res;
    }
    res = push_one(regs_.read(0).value_or(0));
    if (!res) {
        return res;
    }

    data_t exc_return = in_handler_mode_ ? 0xFFFFFFF1u : 0xFFFFFFF9u;
    res = write_reg(14, exc_return);
    if (!res) {
        return res;
    }

    if (!bus_) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    auto handler = bus_->read(vector_addr, Width::Word);
    if (!handler) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    if (*handler == 0) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }

    res = write_reg(15, *handler & ~1u);
    if (!res) {
        return res;
    }

    in_handler_mode_ = true;
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_entry(uint8_t irq_n) {
    addr_t vector_addr =
        vector_table_base_ + 4u * (static_cast<addr_t>(irq_n) + 16);
    auto res = exception_entry_common(vector_addr);
    if (!res) {
        return res;
    }
    current_priority_ = nvic_->irq_priority(irq_n);
    nvic_->clear_pending(irq_n);
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::interrupt_return(data_t exc_return) {
    // Pop {R0, R1, R2, R3, R12, LR, PC, xPSR}
    auto pop_one = [&]() -> CPUExpected<data_t> {
        auto r = pop_stack();
        if (!r) {
            return std::unexpected{CPUError::ExceptionReturnFault};
        }
        return *r;
    };

    auto pop_and_write = [&](uint8_t reg_idx) -> CPUExpected<void> {
        auto v = pop_one();
        if (!v) {
            return std::unexpected{v.error()};
        }
        return write_reg(reg_idx, *v);
    };

    auto res = pop_and_write(0);
    if (!res) {
        return res;
    }
    res = pop_and_write(1);
    if (!res) {
        return res;
    }
    res = pop_and_write(2);
    if (!res) {
        return res;
    }
    res = pop_and_write(3);
    if (!res) {
        return res;
    }
    res = pop_and_write(12);
    if (!res) {
        return res;
    }
    res = pop_and_write(14); // LR
    if (!res) {
        return res;
    }

    auto pc_val = pop_one();
    if (!pc_val) {
        return std::unexpected{pc_val.error()};
    }

    auto xpsr_val = pop_one();
    if (!xpsr_val) {
        return std::unexpected{xpsr_val.error()};
    }
    xpsr_ = *xpsr_val;

    // Set PC (clear bit[0] for address)
    res = write_reg(15, *pc_val & ~1u);
    if (!res) {
        return res;
    }

    // Exit Handler mode
    if (exc_return == 0xFFFFFFF1u) {
        // Return to Handler mode (nested) — keep in_handler_mode_ true
    } else {
        in_handler_mode_ = false;
        current_priority_ = 0xFF;
    }

    return {};
}

CPU::CPUExpected<void>
CortexM3CPU::interrupt_entry_system(uint8_t exception_num) {
    addr_t vector_addr = vector_table_base_ + 4u * exception_num;
    auto res = exception_entry_common(vector_addr);
    if (!res) {
        return res;
    }
    current_priority_ = 0;
    return {};
}

CPU::CPUExpected<void> CortexM3CPU::trigger_hardfault() {
    if (in_handler_mode_) {
        return std::unexpected{CPUError::ExceptionEntryFault};
    }
    return interrupt_entry_system(3);
}

bool CortexM3CPU::try_escalate_fault(CPUError kind, addr_t pc, uint16_t hw1,
                                     uint16_t hw2, bool is32) {
    record_fault(kind, pc, hw1, hw2, is32);
    if (in_handler_mode_) {
        return false;
    }
    auto res = trigger_hardfault();
    return res.has_value();
}

} // namespace micro_forge::cpu::arm::cortex_m3
