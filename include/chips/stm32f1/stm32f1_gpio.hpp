#pragma once

#include "periph/device.hpp"
#include "periph/gpio.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>
#include <functional>

namespace micro_forge::chips::stm32f1 {

class Stm32f1Gpio : public periph::Device, public periph::Gpio {
public:
    explicit Stm32f1Gpio(uint8_t port_id);

    // Device
    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override;

    // Gpio
    void set_pin(uint8_t pin, bool high) override;
    bool get_pin(uint8_t pin) const override;
    void configure_pin(uint8_t pin, periph::PinMode mode,
                       periph::PinPull pull = periph::PinPull::None,
                       periph::PinSpeed speed = periph::PinSpeed::Low) override;
    void simulate_input(uint8_t pin, bool high) override;
    void set_pin_change_callback(PinChangeCallback cb) override;

    WeakPtr<Stm32f1Gpio> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    void on_odr_changed(uint32_t old_odr, uint32_t new_odr);

    uint32_t crl_  = 0x44444444;
    uint32_t crh_  = 0x44444444;
    uint32_t idr_  = 0;
    uint32_t odr_  = 0;
    uint32_t lckr_ = 0;
    uint8_t port_id_;
    PinChangeCallback on_pin_change_;

    WeakPtrFactory<Stm32f1Gpio> weak_factory_{this};
};

} // namespace micro_forge::chips::stm32f1
