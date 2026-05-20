#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace micro_forge::periph {

class NvicPeripheral : public Device {
  public:
    NvicPeripheral() = default;

    // periph::Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "NVIC"; }

    // Query API (CPU hot path — inline)
    bool has_pending_irq() const {
        for (size_t i = 0; i < ispr_.size(); ++i) {
            if (ispr_[i] & iser_[i]) {
                return true;
            }
        }
        return false;
    }

    uint8_t highest_pending_irq() const {
        for (size_t i = 0; i < ispr_.size(); ++i) {
            uint32_t active = ispr_[i] & iser_[i];
            if (active) {
                return static_cast<uint8_t>(i * 32 + __builtin_ctz(active));
            }
        }
        return 0xFF; // no pending
    }

    uint8_t irq_priority(uint8_t irq_n) const {
        if (irq_n >= kMaxIrq) {
            return 0xFF;
        }
        return priorities_[irq_n];
    }

    bool is_enabled(uint8_t irq_n) const {
        return irq_n < kMaxIrq && get_bit(iser_, irq_n);
    }

    // External trigger API
    void set_pending(uint8_t irq_n) {
        if (irq_n < kMaxIrq) {
            set_bit(ispr_, irq_n, true);
        }
    }

    void clear_pending(uint8_t irq_n) {
        if (irq_n < kMaxIrq) {
            set_bit(ispr_, irq_n, false);
        }
    }

    WeakPtr<NvicPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    static constexpr uint8_t kMaxIrq = 240;

    static bool get_bit(const std::array<uint32_t, 8>& arr, uint8_t irq_n) {
        if (irq_n >= kMaxIrq) {
            return false;
        }
        return (arr[irq_n / 32] >> (irq_n % 32)) & 1;
    }

    static void set_bit(std::array<uint32_t, 8>& arr, uint8_t irq_n, bool val) {
        if (irq_n >= kMaxIrq) {
            return;
        }
        if (val) {
            arr[irq_n / 32] |= (1u << (irq_n % 32));
        } else {
            arr[irq_n / 32] &= ~(1u << (irq_n % 32));
        }
    }

    std::array<uint32_t, 8> iser_{};
    std::array<uint32_t, 8> ispr_{};
    std::array<uint8_t, kMaxIrq> priorities_{};

    WeakPtrFactory<NvicPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
