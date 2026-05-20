#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace micro_forge::sim {

// 由芯片配置层提供，描述一个时钟域的初始频率
struct DomainConfig {
    uint32_t freq_hz;
};

class VirtualClock {
  public:
    // 由芯片配置层传入域列表，VirtualClock 本身不知道具体域的含义
    explicit VirtualClock(std::span<const DomainConfig> domains);

    // 每次 CPU step 后调用，推进虚拟时间
    void advance(uint64_t cpu_cycles);

    // 外设 tick 时调用：消耗该域累积的 tick 数并清零
    uint64_t consume_ticks(size_t domain_index);

    // 芯片时钟控制外设（如 RCC）调用，更新域频率
    void set_domain_freq(size_t domain_index, uint32_t freq_hz);

    // 查询域当前频率（USART 算波特率等场景）
    uint32_t domain_freq_hz(size_t domain_index) const;

    // 域数量
    size_t domain_count() const { return domains_.size(); }

    // SYSCLK 域的频率（构造时的第一个域），用于 advance 中纳秒换算
    uint32_t sysclk_freq_hz() const { return sysclk_freq_hz_; }

    // 虚拟时间（纳秒），调试用
    uint64_t total_ns() const { return total_ns_; }

  private:
    struct DomainInfo {
        uint32_t freq_hz = 0;
        uint64_t elapsed_ticks = 0;
        uint64_t residual = 0; // 频率比计算余数（单位：sysclk_freq_hz 分之 1）
    };

    uint32_t sysclk_freq_hz_;
    uint64_t total_ns_ = 0;
    uint64_t total_ns_residual_ = 0;
    std::vector<DomainInfo> domains_;
};

} // namespace micro_forge::sim
