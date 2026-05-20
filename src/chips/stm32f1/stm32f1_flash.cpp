#include "chips/stm32f1/stm32f1_flash.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Flash::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            return acr_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> Stm32f1Flash::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00:
            acr_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

} // namespace micro_forge::chips::stm32f1
