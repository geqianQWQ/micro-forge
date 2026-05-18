#include "sim/coordinator.hpp"

namespace micro_forge::sim {

SimulationCoordinator::SimulationCoordinator(VirtualClock clock)
    : clock_(std::move(clock)) {}

void SimulationCoordinator::set_cpu(WeakPtr<cpu::CPU> cpu) {
    cpu_ = std::move(cpu);
}

void SimulationCoordinator::add_tickable(WeakPtr<periph::Device> dev,
                                         size_t domain_index) {
    tickables_.push_back({std::move(dev), domain_index});
}

cpu::CPU::CPUExpected<void> SimulationCoordinator::step() {
    if (!cpu_.IsValid()) {
        return std::unexpected(cpu::CPU::CPUError::NotRunning);
    }

    auto prev_result = cpu_->cycles();
    if (!prev_result.has_value()) {
        return std::unexpected(prev_result.error());
    }
    uint64_t prev = *prev_result;

    auto step_result = cpu_->step();
    if (!step_result.has_value()) {
        return step_result;
    }

    auto curr_result = cpu_->cycles();
    if (!curr_result.has_value()) {
        return std::unexpected(curr_result.error());
    }
    uint64_t delta = *curr_result - prev;

    if (delta > 0) {
        clock_.advance(delta);

        for (auto& t : tickables_) {
            if (!t.device.IsValid()) {
                continue;
            }
            uint64_t ticks = clock_.consume_ticks(t.domain_index);
            if (ticks > 0) {
                t.device->tick(ticks);
            }
        }
    }

    return {};
}

void SimulationCoordinator::run(size_t max_steps) {
    for (size_t i = 0; i < max_steps; ++i) {
        auto result = step();
        if (!result.has_value()) {
            break;
        }

        auto state_result = cpu_->state();
        if (!state_result.has_value()) {
            break;
        }
        if (*state_result != cpu::CPU::State::Running) {
            break;
        }
    }
}

} // namespace micro_forge::sim
