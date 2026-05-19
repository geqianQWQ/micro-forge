#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"
#include <cstdint>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Flash : public periph::Device {
  public:
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "FLASH"; }

    WeakPtr<Stm32f1Flash> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    // FLASH_ACR at offset 0x00
    uint32_t acr_ = 0x00000030; // PRFTBE=1, LATENCY=1 (default after reset)

    WeakPtrFactory<Stm32f1Flash> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
