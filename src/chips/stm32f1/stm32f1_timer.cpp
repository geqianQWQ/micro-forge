#include "chips/stm32f1/stm32f1_timer.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Timer::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return cr1_;
        case 0x0C:
            return dier_;
        case 0x10:
            return sr_;
        case 0x28:
            return psc_;
        case 0x2C:
            return arr_;
        case 0x34:
            return cnt_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Timer::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            cr1_ = data;
            return {};
        case 0x0C:
            dier_ = data;
            return {};
        case 0x10:
            sr_ = data;
            return {};
        case 0x28:
            psc_ = data;
            return {};
        case 0x2C:
            arr_ = data;
            return {};
        case 0x34:
            cnt_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

void Stm32f1Timer::tick(uint64_t cycles) {
    if (!(cr1_ & 0x0001)) {
        return; // CEN not set
    }

    uint64_t prescaled = cycles / (static_cast<uint64_t>(psc_) + 1);
    cnt_ += static_cast<uint32_t>(prescaled);

    if (arr_ > 0 && cnt_ >= arr_) {
        sr_ |= 0x0001; // UIF
        cnt_ = cnt_ % arr_;
    }
}

void Stm32f1Timer::enable(bool en) {
    if (en) {
        cr1_ |= 0x0001;
    } else {
        cr1_ &= ~0x0001u;
    }
}

void Stm32f1Timer::set_prescaler(uint32_t psc) {
    psc_ = psc;
}
void Stm32f1Timer::set_auto_reload(uint32_t arr) {
    arr_ = arr;
}
uint32_t Stm32f1Timer::counter() const {
    return cnt_;
}
bool Stm32f1Timer::update_flag() const {
    return (sr_ & 0x0001) != 0;
}
void Stm32f1Timer::clear_update_flag() {
    sr_ &= ~0x0001u;
}

} // namespace micro_forge::chips::stm32f1
