#include "chips/stm32f1/peripheral_config.hpp"
#include "core/mem_literal.hpp"

namespace micro_forge::chips::stm32f1 {

using namespace micro_forge::literals;

Expected<void> configure_peripherals(
    memory::Bus& bus,
    Stm32f1Rcc& rcc,
    Stm32f1Gpio& gpioa,
    Stm32f1Gpio& gpiob,
    Stm32f1Gpio& gpioc,
    Stm32f1Usart& usart1,
    Stm32f1Timer& tim2) {

    auto result = bus.map(memory::region(0x4002'1000_addr, 0x400_addr, rcc.GetWeak()));
    if (!result) return result;

    result = bus.map(memory::region(0x4001'0800_addr, 0x400_addr, gpioa.GetWeak()));
    if (!result) return result;

    result = bus.map(memory::region(0x4001'0C00_addr, 0x400_addr, gpiob.GetWeak()));
    if (!result) return result;

    result = bus.map(memory::region(0x4001'1000_addr, 0x400_addr, gpioc.GetWeak()));
    if (!result) return result;

    result = bus.map(memory::region(0x4001'3800_addr, 0x400_addr, usart1.GetWeak()));
    if (!result) return result;

    result = bus.map(memory::region(0x4000'0000_addr, 0x400_addr, tim2.GetWeak()));
    if (!result) return result;

    return {};
}

} // namespace micro_forge::chips::stm32f1
