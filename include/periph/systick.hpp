#pragma once

#include "periph/nvic.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>

namespace micro_forge::periph {

class SysTickPeripheral : public Device {
  public:
    explicit SysTickPeripheral(NvicPeripheral& nvic) : nvic_(nvic) {}

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    std::string_view name() const noexcept override { return "SysTick"; }

    WeakPtr<SysTickPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    static constexpr uint8_t kSysTickIrq = 15;

    uint32_t ctrl_ = 0;
    uint32_t load_ = 0;
    uint32_t val_ = 0;
    NvicPeripheral& nvic_;

    WeakPtrFactory<SysTickPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
