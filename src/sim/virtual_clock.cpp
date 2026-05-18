#include "sim/virtual_clock.hpp"

#include <cassert>

namespace micro_forge::sim {

VirtualClock::VirtualClock(std::span<const DomainConfig> domains)
    : sysclk_freq_hz_(domains.empty() ? 1 : domains[0].freq_hz),
      sysclk_period_ns_(1'000'000'000ULL / sysclk_freq_hz_) {
    domains_.reserve(domains.size());
    for (const auto& cfg : domains) {
        domains_.push_back({cfg.freq_hz, 0, 0});
    }
}

void VirtualClock::advance(uint64_t cpu_cycles) {
    total_ns_ += cpu_cycles * sysclk_period_ns_;

    for (auto& d : domains_) {
        if (d.freq_hz == 0) {
            continue;
        }

        // ticks = cpu_cycles * domain_freq / sysclk_freq
        // 用 __uint128_t 避免溢出
        __uint128_t product = static_cast<__uint128_t>(cpu_cycles) * d.freq_hz;
        uint64_t ticks = static_cast<uint64_t>(product / sysclk_freq_hz_);
        uint64_t rem = static_cast<uint64_t>(product % sysclk_freq_hz_);

        d.residual += rem;
        if (d.residual >= sysclk_freq_hz_) {
            ++ticks;
            d.residual -= sysclk_freq_hz_;
        }

        d.elapsed_ticks += ticks;
    }
}

uint64_t VirtualClock::consume_ticks(size_t domain_index) {
    assert(domain_index < domains_.size());
    auto& d = domains_[domain_index];
    uint64_t result = d.elapsed_ticks;
    d.elapsed_ticks = 0;
    return result;
}

void VirtualClock::set_domain_freq(size_t domain_index, uint32_t freq_hz) {
    assert(domain_index < domains_.size());
    domains_[domain_index].freq_hz = freq_hz;
}

uint32_t VirtualClock::domain_freq_hz(size_t domain_index) const {
    assert(domain_index < domains_.size());
    return domains_[domain_index].freq_hz;
}

} // namespace micro_forge::sim
