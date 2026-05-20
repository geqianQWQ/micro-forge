#include "sim/virtual_clock.hpp"

namespace micro_forge::sim {

VirtualClock::VirtualClock(std::span<const DomainConfig> domains)
    : sysclk_freq_hz_(
          (domains.empty() || domains[0].freq_hz == 0) ? 1
                                                        : domains[0].freq_hz) {
    domains_.reserve(domains.size());
    for (const auto& cfg : domains) {
        domains_.push_back({cfg.freq_hz, 0, 0});
    }
}

void VirtualClock::advance(uint64_t cpu_cycles) {
    __uint128_t ns_product =
        static_cast<__uint128_t>(cpu_cycles) * 1'000'000'000ULL +
        total_ns_residual_;
    total_ns_ += static_cast<uint64_t>(ns_product / sysclk_freq_hz_);
    total_ns_residual_ =
        static_cast<uint64_t>(ns_product % sysclk_freq_hz_);

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
    if (domain_index >= domains_.size()) {
        return 0;
    }
    auto& d = domains_[domain_index];
    uint64_t result = d.elapsed_ticks;
    d.elapsed_ticks = 0;
    return result;
}

void VirtualClock::set_domain_freq(size_t domain_index, uint32_t freq_hz) {
    if (domain_index >= domains_.size()) {
        return;
    }
    domains_[domain_index].freq_hz = freq_hz;
}

uint32_t VirtualClock::domain_freq_hz(size_t domain_index) const {
    if (domain_index >= domains_.size()) {
        return 0;
    }
    return domains_[domain_index].freq_hz;
}

} // namespace micro_forge::sim
