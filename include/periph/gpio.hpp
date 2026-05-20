#pragma once

#include <cstdint>
#include <functional>

namespace micro_forge::periph {

enum class PinMode { Input, Output, Alternate, Analog };
enum class PinPull { None, PullUp, PullDown };
enum class PinSpeed { Low, Medium, High };

class Gpio {
  public:
    virtual ~Gpio() = default;

    virtual void set_pin(uint8_t pin, bool high) = 0;
    virtual bool get_pin(uint8_t pin) const = 0;

    virtual void configure_pin(uint8_t pin, PinMode mode,
                               PinPull pull = PinPull::None,
                               PinSpeed speed = PinSpeed::Low) = 0;

    virtual void simulate_input(uint8_t pin, bool high) = 0;

    using PinChangeCallback = std::function<void(uint8_t pin, bool high)>;
    virtual void set_pin_change_callback(PinChangeCallback cb) = 0;
};

} // namespace micro_forge::periph
