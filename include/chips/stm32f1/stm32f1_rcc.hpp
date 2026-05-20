#pragma once

#include "periph/clock_controller.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Rcc : public periph::Device, public periph::ClockController {
  public:
    Stm32f1Rcc() = default;

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "RCC"; }

    // ClockController
    bool is_clock_enabled(uint32_t peripheral_addr) const override;
    void enable_clock(uint32_t peripheral_addr) override;

    WeakPtr<Stm32f1Rcc> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    uint32_t cr_ = 0x00000083;
    uint32_t cfgr_ = 0x00000000;
    uint32_t cir_ = 0x00000000;
    uint32_t ahbenr_ = 0x00000000;
    uint32_t apb2enr_ = 0x00000000;
    uint32_t apb1enr_ = 0x00000000;

    WeakPtrFactory<Stm32f1Rcc> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
