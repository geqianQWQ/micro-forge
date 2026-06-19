#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"
#include <cstdint>
#include <functional>

namespace micro_forge::periph {

class ScbPeripheral : public Device {
  public:
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "SCB"; }

    WeakPtr<ScbPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

    void set_vtor_callback(std::function<void(uint32_t)> cb) {
        vtor_cb_ = std::move(cb);
    }

    void set_prigroup_callback(std::function<void(uint8_t)> cb) {
        prigroup_cb_ = std::move(cb);
    }

    // Priority of system exception `exc_num` (4..15), stored in SHPR1/2/3.
    // Returns 0xFF for exceptions without a configurable priority.
    // Note: HardFault(3), NMI(2), Reset(1) have fixed priority and are not
    // looked up here.
    uint8_t system_exception_priority(uint8_t exc_num) const noexcept {
        if (exc_num >= 4 && exc_num <= 15) {
            return shp_[exc_num - 4];
        }
        return 0xFF;
    }

  private:
    uint32_t cpuid_ = 0x412FC230; // Cortex-M3 r2p0
    uint32_t icsr_ = 0;
    uint32_t vtor_ = 0;
    uint32_t aircr_ = 0xFA050000;
    uint32_t scr_ = 0;
    uint32_t ccr_ = 0x00000200; // STKALIGN
    uint8_t shp_[12] = {};
    uint32_t shcsr_ = 0;

    std::function<void(uint32_t)> vtor_cb_;
    std::function<void(uint8_t)> prigroup_cb_;

    WeakPtrFactory<ScbPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
