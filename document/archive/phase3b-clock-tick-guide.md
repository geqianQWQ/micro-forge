# Phase 3B 前置：定时节拍器实施手册

> 日期: 2026-05-18
> 决策来源: document/notes/003-clock-tick-abstraction-proposal.md（方案 B+）
> 演进方向: 方案 D（EventScheduler）
> 状态: **已实施，89/89 测试全绿**

---

## 目标

在 Phase 3B 正式实施 NVIC/SysTick 之前，先建立时钟基础设施：
1. `DomainConfig` / `VirtualClock` — 通用虚拟时钟（`sim/` 命名空间）
2. `ClockDomain` — STM32F103 专用时钟域标识（`chips/stm32f1/` 命名空间）
3. `SimulationCoordinator` — 仿真协调器，驱动 CPU step + 外设 tick 传播

---

## 已实施的文件

| 文件 | 类型 | 说明 |
|------|------|------|
| `include/sim/virtual_clock.hpp` | 通用 | VirtualClock + DomainConfig |
| `src/sim/virtual_clock.cpp` | 通用 | 频率比算法（__uint128_t 精度） |
| `include/sim/coordinator.hpp` | 通用 | SimulationCoordinator + Tickable |
| `src/sim/coordinator.cpp` | 通用 | step/run 实现 |
| `include/chips/stm32f1/clock_domains.hpp` | 芯片 | ClockDomain enum + stm32f103_default_clocks |
| `test/test_virtual_clock.cpp` | 测试 | 6 个 VirtualClock 测试 |
| `test/test_coordinator.cpp` | 测试 | 3 个 Coordinator 测试 |

### 修改的现有文件

| 文件 | 变更 |
|------|------|
| `include/arch/arm/cortex_m3/cortex_m3.hpp` | 添加 WeakPtrFactory + GetWeak() |
| `test/CMakeLists.txt` | 添加 test_sim 测试目标 |

---

## 接口概览

### VirtualClock（sim/，通用）

```cpp
namespace micro_forge::sim {

struct DomainConfig {
    uint32_t freq_hz;
};

class VirtualClock {
public:
    explicit VirtualClock(std::span<const DomainConfig> domains);
    void advance(uint64_t cpu_cycles);
    uint64_t consume_ticks(size_t domain_index);
    void set_domain_freq(size_t domain_index, uint32_t freq_hz);
    uint32_t domain_freq_hz(size_t domain_index) const;
    size_t domain_count() const;
    uint32_t sysclk_freq_hz() const;
    uint64_t total_ns() const;
};

}
```

**核心算法**：advance() 用频率比直接计算域 tick：
```
ticks = cpu_cycles * domain_freq / sysclk_freq
```
用 `__uint128_t` 避免溢出，余数累积保证长期无漂移。

### SimulationCoordinator（sim/，通用）

```cpp
namespace micro_forge::sim {

struct Tickable {
    WeakPtr<periph::Device> device;
    size_t domain_index;
};

class SimulationCoordinator {
public:
    explicit SimulationCoordinator(VirtualClock clock);
    void set_cpu(WeakPtr<cpu::CPU> cpu);
    void add_tickable(WeakPtr<periph::Device> dev, size_t domain_index);
    cpu::CPU::CPUExpected<void> step();
    void run(size_t max_steps = SIZE_MAX);
    VirtualClock& clock();
};

}
```

**step() 流程**：
```
1. 记录 cpu->cycles()
2. cpu->step()
3. delta = cpu->cycles() - prev
4. clock_.advance(delta)
5. 遍历 tickables_：
   ticks = clock_.consume_ticks(domain_index)
   if ticks > 0 → device->tick(ticks)
```

### ClockDomain（chips/stm32f1/，STM32F103 专用）

```cpp
namespace micro_forge::chips::stm32f1 {

enum class ClockDomain : uint8_t {
    Sysclk = 0,  // CPU + AHB
    Apb1   = 1,  // APB1 总线
    Apb2   = 2,  // APB2 总线
};

static constexpr sim::DomainConfig stm32f103_default_clocks[] = {
    {8'000'000},  // Sysclk
    {8'000'000},  // Apb1
    {8'000'000},  // Apb2
};

inline size_t domain_index(ClockDomain d);

}
```

---

## 使用示例

```cpp
#include "chips/stm32f1/clock_domains.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"

using namespace micro_forge;
using namespace chips::stm32f1;

// 创建协调器（STM32F103 默认时钟）
VirtualClock clk(stm32f103_default_clocks);
SimulationCoordinator coordinator(std::move(clk));

// 接入 CPU
coordinator.set_cpu(cpu.GetWeak());

// 接入外设
coordinator.add_tickable(systick.GetWeak(),
                         domain_index(ClockDomain::Sysclk));
coordinator.add_tickable(tim2.GetWeak(),
                         domain_index(ClockDomain::Apb1));

// 运行仿真
coordinator.run(10000);
```

---

## 后续集成点

### Phase 3B：SysTick + NVIC

- SysTickPeripheral 注册到 Sysclk 域
- NvicPeripheral 不需要 tick（被动查询），但挂在 Bus 上
- coordinator.run() 替代现有的 for 循环 step

### Phase 4：RCC + TIM2 + USART

- RccPeripheral 写入 CFGR 时调 `coordinator.clock().set_domain_freq()`
- TIM2Peripheral 注册到 Apb1 域
- USART 用 `clock.domain_freq_hz()` 计算波特率

### Phase 5+：EventScheduler 演进

- 高频外设（SysTick, TIM）迁移到事件注册模式
- coordinator.step() 增加一路 `scheduler.run_due_events()`
- 低频外设保持现有 tickables 模式
