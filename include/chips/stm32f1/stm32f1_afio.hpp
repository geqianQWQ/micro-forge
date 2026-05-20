#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"
#include <cstdint>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Afio : public periph::Device {
  public:
    Stm32f1Afio() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "AFIO"; }

    WeakPtr<Stm32f1Afio> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t evcr_    = 0x00000000;
    uint32_t mapr_    = 0x00000000;
    uint32_t exticr_[4] = {};
    uint32_t mapr2_   = 0x00000000;

    WeakPtrFactory<Stm32f1Afio> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
