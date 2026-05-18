#pragma once

#include "cpu/cpu.hpp"
#include "memory/bus.hpp"
#include "sim/coordinator.hpp"
namespace micro_forge::chips {
/**
 * @brief When you hold a minimal chip, you hold a machine
 *
 */
struct Machine {
    std::expected<void, std::string> load_bin(uint32_t base,
                                              std::span<const uint8_t> data);
    std::expected<void, std::string> load_elf(std::span<const uint8_t> data);

    void run(size_t max_steps = SIZE_MAX);

    /* public visit */
    Machine() = default;
    std::unique_ptr<sim::SimulationCoordinator> coord;
    std::unique_ptr<cpu::CPU> cpu;
    std::unique_ptr<memory::Bus> bus;

  private:
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;
};
} // namespace micro_forge::chips