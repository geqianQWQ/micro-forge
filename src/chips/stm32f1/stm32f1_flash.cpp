#include "chips/stm32f1/stm32f1_flash.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Flash::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00: return acr_;
        case 0x04: return keyr_;
        case 0x08: return optkeyr_;
        case 0x0C: return sr_;
        case 0x10: return cr_;
        default:   return 0u; // Reserved reads as 0
    }
}

Expected<void> Stm32f1Flash::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00: acr_ = data;     return {};
        case 0x04: keyr_ = data;    return {}; // Accept write, no unlock logic
        case 0x08: optkeyr_ = data; return {};
        case 0x0C: sr_ = data;      return {}; // W1C bits accepted
        case 0x10: cr_ = data;      return {};
        default:   return {}; // Reserved writes ignored
    }
}

} // namespace micro_forge::chips::stm32f1
