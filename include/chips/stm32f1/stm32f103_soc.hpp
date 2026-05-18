#pragma once

#include "chips/machine.hpp"
#include "chips/stm32f1/clock_domains.hpp"
#include "chips/stm32f1/stm32f1_rcc.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "memory/flat_memory.hpp"
#include "periph/clock_controller.hpp"
#include "periph/gpio.hpp"
#include "periph/serial_port.hpp"
#include "periph/timer.hpp"
#include "periph/nvic.hpp"
#include "periph/systick.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace micro_forge::chips::stm32f1 {

struct Stm32f103Parts {
    memory::FlatMemory flash{128 * 1024};
    memory::FlatMemory sram{20 * 1024};

    periph::NvicPeripheral nvic;
    periph::SysTickPeripheral systick;

    Stm32f1Rcc rcc;
    Stm32f1Gpio gpioa{'A'};
    Stm32f1Gpio gpiob{'B'};
    Stm32f1Gpio gpioc{'C'};
    Stm32f1Usart usart1;
    Stm32f1Timer tim2;

    Stm32f103Parts() : systick(nvic) {}

    periph::Gpio& gpio(char id);
    periph::SerialPort& serial() { return usart1; }
    periph::Timer& timer() { return tim2; }
    periph::ClockController& clocks() { return rcc; }
};

class Stm32f103Soc {
public:
    static std::expected<std::unique_ptr<Stm32f103Soc>, std::string> create();

    chips::Machine& machine() { return machine_; }
    Stm32f103Parts& parts() { return parts_; }

    std::expected<void, std::string> load_elf(std::span<const uint8_t> data);
    std::expected<void, std::string> load_bin(uint32_t base,
                                               std::span<const uint8_t> data);
    void run(size_t max_steps = SIZE_MAX);

    Stm32f103Soc(const Stm32f103Soc&) = delete;
    Stm32f103Soc& operator=(const Stm32f103Soc&) = delete;
    Stm32f103Soc(Stm32f103Soc&&) = delete;
    Stm32f103Soc& operator=(Stm32f103Soc&&) = delete;

private:
    Stm32f103Soc() = default;

    chips::Machine machine_;
    Stm32f103Parts parts_;
};

} // namespace micro_forge::chips::stm32f1
