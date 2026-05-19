#pragma once

#include "chips/stm32f1/stm32f1_flash.hpp"
#include "chips/stm32f1/stm32f1_rcc.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "memory/bus.hpp"

namespace micro_forge::chips::stm32f1 {

Expected<void> configure_peripherals(
    memory::Bus& bus,
    Stm32f1Rcc& rcc,
    Stm32f1Flash& flash_if,
    Stm32f1Gpio& gpioa,
    Stm32f1Gpio& gpiob,
    Stm32f1Gpio& gpioc,
    Stm32f1Usart& usart1,
    Stm32f1Timer& tim2);

} // namespace micro_forge::chips::stm32f1
