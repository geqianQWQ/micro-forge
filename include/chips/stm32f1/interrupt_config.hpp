#pragma once

#include "memory/bus.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "periph/systick.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<void> configure_interrupt_devices(memory::Bus& bus,
                                           periph::NvicPeripheral& nvic,
                                           periph::SysTickPeripheral& systick,
                                           periph::ScbPeripheral& scb);

} // namespace micro_forge::chips::stm32f1
