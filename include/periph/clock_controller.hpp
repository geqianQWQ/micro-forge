#pragma once

#include <cstdint>

namespace micro_forge::periph {

class ClockController {
public:
    virtual ~ClockController() = default;

    virtual bool is_clock_enabled(uint32_t peripheral_addr) const = 0;
    virtual void enable_clock(uint32_t peripheral_addr) = 0;
};

} // namespace micro_forge::periph
