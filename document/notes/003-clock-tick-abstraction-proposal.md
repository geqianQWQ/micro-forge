# 定时节拍器抽象设计提案

> 日期: 2026-05-18
> 阶段: Phase 3B 前置设计（影响 3B ~ 5+）
> 状态: 已决定 — 采用方案 B+（D 为演进方向）

---

## 1. 问题陈述

当前 micro-forge 模拟器的现状：

- `periph::Device` 有 `tick(uint64_t cycles)` 虚方法（默认空实现）
- CPU 的 `step()` 自增 `cycles_` 计数器
- **没有任何基础设施调用过 `tick()`**
- Bus 不传播 tick，没有仿真协调器

这个缺口目前无害，因为总线上只有 `FlatMemory`（无时序状态）。但 Phase 3B 的 SysTick 需要逐周期倒计时，Phase 4 的 TIM2 需要正确的 APB 分频，Phase 4 的 RCC 会动态修改总线频率。

**核心问题**：在"CPU 执行了 N 周期"和"外设感知到时间流逝"之间，应该放什么抽象层？

---

## 2. 硬件背景（STM32F103 实际时钟架构）

```
HSI (8MHz) ─┐
             ├─→ PLL ──→ SYSCLK (max 72MHz) ──→ CPU
HSE (8MHz) ─┘         │
                       ├─→ AHB (HPRE 分频) ──→ SysTick, DMA
                       │
                       ├─→ APB1 (PPRE1 分频, max 36MHz) ──→ TIM2-7, USART2-5, I2C, SPI2
                       │
                       └─→ APB2 (PPRE2 分频, max 72MHz) ──→ GPIO, USART1, TIM1, ADC
```

关键事实：
- SysTick 直接数 CPU 周期（SYSCLK 频率）
- TIM2 的输入是 APB1 时钟，APB1 可以被 RCC 的 PPRE1 分频
- USART 波特率由其所在总线频率 + BRR 寄存器共同决定
- RCC 可以在运行时重配 PLL、AHB/APB 分频器

---

## 3. 业界参考

### QEMU（双层架构）

- **层 1 — 时间域**：`QEMU_CLOCK_VIRTUAL` 追踪虚拟纳秒，由指令计数驱动（icount 模式），确定性
- **层 2 — 硬件时钟树**：`Clock` QOM 对象，用周期（2^-32 ns 精度）建模，递归传播 + 内置 mul/div
- STM32F2xx 定时器在 `QEMU_CLOCK_VIRTUAL` 上创建 `QEMUTimer`，用设备频率将虚拟纳秒转为定时器周期

### Renode（量子模型）

- 主时间源产生固定量子（如 1us）的虚拟时间
- 时间汇（CPU）执行量子后屏障同步
- 外设用 `LimitTimer` 连接机器时钟源，按虚拟时间触发事件
- **无硬件时钟树**——频率是静态构造参数

### 对比

| 关注点 | QEMU | Renode |
|--------|------|--------|
| 时钟树建模 | 完整 Clock QOM 对象，递归传播 | 无，频率为构造参数 |
| 动态频率变更 | 时钟回调自动触发重算 | 需手动处理 |
| 确定性 | icount 模式下确定 | 始终确定 |
| 复杂度 | 高（两套正交系统） | 中（单一框架） |

---

## 4. 四个方案

### 方案 A：最小 — SimulationCoordinator

一个协调器：调 `cpu->step()`，算 delta cycles，遍历调 `tick(delta)`。

```cpp
class SimulationCoordinator {
    WeakPtr<cpu::CPU> cpu_;
    std::vector<WeakPtr<periph::Device>> peripherals_;
    uint64_t last_cycles_ = 0;

    cpu::CPU::CPUExpected<void> step();
    void run(size_t max_steps = SIZE_MAX);
};
```

**tick 传播**：step() → 算 delta → 遍历 tick(delta)

**SysTick**：直接用 CPU 周期数，正确。
**TIM2**：`cnt_ += cycles`，假设 APB1 频率 = CPU 频率。在默认复位条件下成立，固件重配 RCC 后出错。
**RCC**：纯寄存器文件，写入不影响实际频率。

| 维度 | 评价 |
|------|------|
| 新增代码 | ~50 行 |
| 工期 | 半天 |
| Phase 3B 正确性 | 完全正确（SysTick 只关心 CPU 周期） |
| Phase 4 正确性 | 默认时钟下正确，重配 RCC 后错误 |
| 扩展性 | 需重设计 |

### 方案 B：适中 — VirtualClock + 命名域

引入 `VirtualClock` 追踪虚拟时间，外设声明所属时钟域，RCC 可更新域频率。

```cpp
enum class ClockDomain : uint8_t {
    Sysclk,   // CPU + AHB
    Apb1,     // APB1 外设
    Apb2,     // APB2 外设
};

class VirtualClock {
    uint32_t sysclk_hz_;
    std::array<ClockInfo, 3> domains_;
    std::array<uint64_t, 3> residual_ns_;

    void advance(uint64_t cpu_cycles);
    uint64_t consume_domain_ticks(ClockDomain domain);
    void set_domain_freq(ClockDomain domain, uint32_t freq_hz);
};
```

**SysTick**：Sysclk 域，ticks = CPU 周期数，正确。
**TIM2**：Apb1 域，ticks = APB1 时钟周期数。PSC 再分频。正确。
**RCC**：写入 CFGR 时调 `set_domain_freq`，外设下次 tick 立即生效。

| 维度 | 评价 |
|------|------|
| 新增代码 | ~200 行 |
| 工期 | 1-2 天 |
| Phase 3B 正确性 | 完全正确 |
| Phase 4 正确性 | 正确（含 RCC 重配） |
| 扩展性 | 新芯片需新 enum |

### 方案 C：完整 — QEMU 式时钟树

用 `ClockNode` 有向无环图建模真实时钟分配网络。节点含周期 + mul/div，递归传播。

| 维度 | 评价 |
|------|------|
| 新增代码 | ~500+ 行 |
| 工期 | 4-5 天 |
| 扩展性 | 任意芯片拓扑 |
| 调试负担 | 中高（QEMU 团队花数年修时钟传播 bug） |
| 是否必要 | 单芯片模拟器不必要 |

### 方案 D：事件驱动 + 时钟域（B 的长期形态）

核心思想：不是每次 CPU step 都遍历所有外设 tick()，而是让会产生时间事件的外设注册"下一次事件发生在什么时候"。

```cpp
class VirtualClock {
public:
    void advance_cpu_cycles(uint64_t cycles);
    uint64_t now_ticks(ClockDomainId domain) const;
    uint64_t convert_ticks(ClockDomainId from, ClockDomainId to, uint64_t ticks) const;
};

class EventScheduler {
public:
    using Callback = std::function<void()>;
    EventId schedule(ClockDomainId domain, uint64_t delay_ticks, Callback cb);
    void cancel(EventId id);
    void run_due_events();
};
```

**典型场景**：SysTick 配置 LOAD=1000 后，不需要每条指令都 val_--，而是注册一个"Core 域 1000 ticks 后触发"的事件。coordinator 每次 CPU step 后推进虚拟时间，然后只执行到期事件。

**优势**：外设多起来后（TIM、USART timeout、DMA completion），不会每条指令扫一遍几十个外设。

**最终架构**：

```
CPU step
  → VirtualClock advance
  → EventScheduler run_due_events
  → legacy tickables tick(delta)   // 过渡层，可逐步减少
```

---

## 5. 对比总表

| 维度 | 方案 A | 方案 B | 方案 C | 方案 D |
|------|--------|--------|--------|--------|
| 新增代码 | ~50 行 | ~200 行 | ~500+ 行 | ~400 行（增量） |
| 实现工期 | 半天 | 1-2 天 | 4-5 天 | 2-3 天 |
| SysTick 正确性 | ✓ | ✓ | ✓ | ✓ |
| TIM2 默认时钟 | ✓ | ✓ | ✓ | ✓ |
| TIM2 RCC 重配后 | ✗ | ✓ | ✓ | ✓ |
| USART 波特率 | ✗ | ✓ | ✓ | ✓ |
| 多外设性能 | O(N) 全扫 | O(N) 全扫 | O(N) 全扫 | O(到期事件数) |
| 多芯片扩展 | 需重设计 | 新 enum | 加节点 | 加域 + 事件 |
| 调试难度 | 无 | 低 | 中高 | 中 |

---

## 6. 决定：B+ 起步，D 作为演进方向

**Phase 3B**：实现方案 B 的核心（VirtualClock + SimulationCoordinator + ClockDomain）。
**Phase 4/5**：保持 tick 模型，跑通真实固件。
**后续**：外设数量上来后，把高频 tick 外设迁移到 EventScheduler。

这个路线比 A 更有远见，比 C 更克制，也比单纯 B 更清楚地回答了"多单片机以后怎么办"。

实施手册见：`document/milestones/phase3b-clock-tick-guide.md`
