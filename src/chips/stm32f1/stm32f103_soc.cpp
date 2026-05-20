#include "chips/stm32f1/stm32f103_soc.hpp"
#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "arch/arm/cortex_m3/cortex_m3_reset.hpp"
#include "chips/stm32f1/interrupt_config.hpp"
#include "chips/stm32f1/memory_bus.hpp"
#include "chips/stm32f1/peripheral_config.hpp"

namespace micro_forge::chips::stm32f1 {

periph::Gpio& Stm32f103Parts::gpio(char id) {
    switch (id) {
        case 'A':
            return gpioa;
        case 'B':
            return gpiob;
        case 'C':
            return gpioc;
        default:
            return gpioa;
    }
}

std::expected<std::unique_ptr<Stm32f103Soc>, std::string>
Stm32f103Soc::create() {
    auto soc = std::unique_ptr<Stm32f103Soc>(new Stm32f103Soc());
    auto& m = soc->machine_;
    auto& p = soc->parts_;

    // Bus
    m.bus = std::make_unique<memory::Bus>();

    // Memory: Flash + SRAM
    auto r = configure_memory(*m.bus, p.flash, p.sram);
    if (!r) {
        return std::unexpected("failed to configure memory");
    }

    // Interrupt devices: NVIC + SysTick
    r = configure_interrupt_devices(*m.bus, p.nvic, p.systick, p.scb);
    if (!r) {
        return std::unexpected("failed to configure interrupt devices");
    }

    // Peripherals: RCC + GPIO + USART + Timer
    r = configure_peripherals(*m.bus, p.rcc, p.afio, p.flash_if, p.gpioa,
                              p.gpiob, p.gpioc, p.usart1, p.tim2);
    if (!r) {
        return std::unexpected("failed to configure peripherals");
    }

    // CPU
    auto* cm3 = new cpu::arm::cortex_m3::CortexM3CPU(m.bus->GetWeak());
    m.cpu.reset(cm3);
    cm3->set_nvic(p.nvic);

    // Wire SysTick → CPU system exception (bypasses NVIC)
    p.systick.set_irq_callback([cm3]() { cm3->sys_tick_irq(); });

    // Wire SCB VTOR write → CPU vector_table_base_ update
    p.scb.set_vtor_callback(
        [cm3](uint32_t vtor) { cm3->set_vector_table_base(vtor); });

    // SimulationCoordinator
    auto clock = sim::VirtualClock(std::span<const sim::DomainConfig>(
        stm32f103_default_clocks, kClockDomainCount));
    m.coord = std::make_unique<sim::SimulationCoordinator>(std::move(clock));
    m.coord->set_cpu(cm3->GetWeak());
    m.coord->add_tickable(p.systick.GetWeak(),
                          domain_index(ClockDomain::Sysclk));
    m.coord->add_tickable(p.tim2.GetWeak(), domain_index(ClockDomain::Apb1));

    return soc;
}

std::expected<void, std::string>
Stm32f103Soc::load_elf(std::span<const uint8_t> data) {
    auto r = machine_.load_elf(data);
    if (!r) {
        return r;
    }

    auto* cm3 =
        static_cast<cpu::arm::cortex_m3::CortexM3CPU*>(machine_.cpu.get());
    auto reset_r = cpu::arm::cortex_m3::cortex_m3_reset(*cm3, *machine_.bus);
    if (!reset_r) {
        return reset_r;
    }

    cm3->launch();
    return {};
}

std::expected<void, std::string>
Stm32f103Soc::load_bin(uint32_t base, std::span<const uint8_t> data) {
    return machine_.load_bin(base, data);
}

micro_forge::sim::RunResult Stm32f103Soc::run(size_t max_steps) {
    return machine_.run(max_steps);
}

} // namespace micro_forge::chips::stm32f1
