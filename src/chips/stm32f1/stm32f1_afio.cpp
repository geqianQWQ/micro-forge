#include "chips/stm32f1/stm32f1_afio.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<data_t> Stm32f1Afio::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00: return evcr_;
        case 0x04: return mapr_;
        case 0x08: return exticr_[0];
        case 0x0C: return exticr_[1];
        case 0x10: return exticr_[2];
        case 0x14: return exticr_[3];
        case 0x18: return mapr2_;
        default:   return 0u;
    }
}

Expected<void> Stm32f1Afio::write(addr_t offset, data_t data, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }
    switch (offset) {
        case 0x00: evcr_      = data; return {};
        case 0x04: mapr_      = data; return {};
        case 0x08: exticr_[0] = data; return {};
        case 0x0C: exticr_[1] = data; return {};
        case 0x10: exticr_[2] = data; return {};
        case 0x14: exticr_[3] = data; return {};
        case 0x18: mapr2_     = data; return {};
        default:   return {};
    }
}

} // namespace micro_forge::chips::stm32f1
