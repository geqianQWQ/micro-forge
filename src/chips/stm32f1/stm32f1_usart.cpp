#include "chips/stm32f1/stm32f1_usart.hpp"

#include <cstdio>

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Usart::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return sr_;
        case 0x04:
            return dr_;
        case 0x08:
            return brr_;
        case 0x0C:
            return cr1_;
        case 0x10:
            return cr2_;
        case 0x14:
            return cr3_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Usart::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            sr_ = data;
            return {};
        case 0x04: {
            dr_ = data;
            uint8_t ch = data & 0xFF;
            if (output_) {
                output_(ch);
            } else {
                putchar(ch);
            }
            return {};
        }
        case 0x08:
            brr_ = data;
            return {};
        case 0x0C:
            cr1_ = data;
            return {};
        case 0x10:
            cr2_ = data;
            return {};
        case 0x14:
            cr3_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

void Stm32f1Usart::send_byte(uint8_t byte) {
    dr_ = byte;
    if (output_) {
        output_(byte);
    } else {
        putchar(byte);
    }
}

bool Stm32f1Usart::can_send() const {
    return (sr_ & 0x80) != 0; // TXE always 1
}

void Stm32f1Usart::set_output(OutputCallback cb) {
    output_ = std::move(cb);
}

} // namespace micro_forge::chips::stm32f1
