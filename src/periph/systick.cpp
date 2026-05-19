#include "periph/systick.hpp"

namespace micro_forge::periph {

// MMIO offsets (relative to SysTick base 0xE000E010):
//   0x00  CTRL      bit0=ENABLE, bit1=TICKINT, bit2=CLKSOURCE, bit16=COUNTFLAG
//   0x04  LOAD      24-bit reload value
//   0x08  VAL       24-bit current value (write-clear)
//   0x0C  CALIB     read-only

Expected<data_t> SysTickPeripheral::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return ctrl_;
        case 0x04:
            return load_;
        case 0x08:
            return val_;
        case 0x0C:
            return 0; // CALIB
        default:
            return std::unexpected(BusError::Unmapped);
    }
}

Expected<void> SysTickPeripheral::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00: {
            bool was_enabled = ctrl_ & 0x1;
            ctrl_ = data;
            // ARM spec: ENABLE 0→1 loads VAL from LOAD
            if (!was_enabled && (data & 0x1)) {
                val_ = load_;
            }
            return {};
        }
        case 0x04:
            load_ = data & 0x00FFFFFFu;
            return {};
        case 0x08:
            // Writing any value to VAL clears both VAL and COUNTFLAG
            val_ = 0;
            ctrl_ &= ~(1u << 16);
            return {};
        case 0x0C:
            return std::unexpected(BusError::ReadOnly);
        default:
            return std::unexpected(BusError::Unmapped);
    }
}

void SysTickPeripheral::tick(uint64_t cycles) {
    if (!(ctrl_ & 0x1)) {
        return; // ENABLE not set
    }

    while (cycles > 0) {
        if (val_ == 0 && load_ == 0) {
            // Both zero: set COUNTFLAG once then stop
            ctrl_ |= (1u << 16);
            break;
        }
        if (val_ > cycles) {
            val_ -= static_cast<uint32_t>(cycles);
            break;
        }
        // Countdown reaches zero
        cycles -= val_;
        ctrl_ |= (1u << 16); // COUNTFLAG
        val_ = load_;        // Auto-reload
        if (ctrl_ & 0x2) {   // TICKINT
            if (irq_cb_) {
                irq_cb_();
            }
        }
    }
}

} // namespace micro_forge::periph
