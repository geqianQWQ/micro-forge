# Phase 3C · NVIC + SysTick + 中断

> 预计工期：1-2 周 | 依赖：Phase 3B（时钟基础设施） | 状态：待实施

## 目标

实现 Cortex-M3 的中断子系统：NVIC 外设、中断响应（压栈+跳转）、中断返回（弹栈）、SysTick 定时器。
完成后，SysTick 中断能正确触发并返回。

---

## 设计决策

### D3C-1：NVIC 架构（核心设计点）

NVIC 是本项目最特殊的外设——它对外是 `periph::Device`（MMIO 读写），对内被 CPU 每步查询（pull 模式，不持有 CPU 引用）。

**方案：NVIC 分两层**

```cpp
// 对外：periph::Device（挂在 MemoryBus 上，处理 MMIO 读写）
// 不持有 CPU 引用——中断响应由 CPU 在 step() 中主动查询（pull 模式）
class NvicPeripheral : public periph::Device {
    std::array<uint32_t, 8> iser_{};   // Interrupt Set Enable
    std::array<uint32_t, 8> icer_{};   // Interrupt Clear Enable
    std::array<uint32_t, 8> ispr_{};   // Interrupt Set Pending
    std::array<uint32_t, 8> icpr_{};   // Interrupt Clear Pending
    std::array<uint32_t, 60> ip_{};    // Interrupt Priority (8-bit each)

public:
    NvicPeripheral() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void>   write(addr_t offset, data_t val, Width w) override;
    std::string_view name() const noexcept override { return "NVIC"; }

    // 查询接口（CPU 调用）
    bool has_pending_irq() const;
    uint8_t highest_pending_irq() const;
    uint8_t irq_priority(uint8_t irq_n) const;
    bool is_enabled(uint8_t irq_n) const;

    // 外部触发接口（SysTick 等调用）
    void set_pending(uint8_t irq_n);
    void clear_pending(uint8_t irq_n);
};
```

**数据流方向**（无循环引用）：
```
SysTick ──→ NVIC.set_pending(15)     // tick 触发
CPU     ──→ NVIC.has_pending_irq()   // 每步查询（pull 模式）
CPU     ──→ NVIC.highest_pending_irq()
```

```cpp
// CPU 内部的中断处理逻辑（在 CortexM3CPU 中）
class CortexM3CPU : public cpu::CPU {
    NvicPeripheral& nvic_;  // CPU 持有 NVIC 引用（单向）

    // 中断处理（CPU 内部）
    void check_and_handle_interrupt();
    void interrupt_entry(uint8_t irq_n);
    void interrupt_return(uint32_t exc_return_val);

    // EXC_RETURN 统一检测——BX LR 和 POP {PC} 都走此路径
    void write_pc(uint32_t val) {
        if (in_handler_mode_ && val >= 0xFFFFFFF1) {
            interrupt_return(val);   // 弹栈恢复
        } else {
            regs_.pc = val & ~1u;    // 正常跳转，bit[0] 清零
        }
    }

    bool in_handler_mode_ = false;
    uint8_t current_priority_ = 0;   // 第一版不嵌套，留作扩展
};
```

### D3C-2：中断响应流程

```
1. SimulationCoordinator.step() 调用 cpu->step()
   cpu->step() 内部在执行指令前调用 check_and_handle_interrupt()
2. 查询 NVIC：has_pending_irq()? highest_pending_irq()?
3. 如果有 pending 且优先级高于当前：
   a. 保存上下文到栈：{R0, R1, R2, R3, R12, LR, PC, xPSR}
      压栈顺序必须按 ARM 规范（地址递减）
   b. 设置 LR = EXC_RETURN 值
      Thread mode: 0xFFFFFFF9
      Handler mode: 0xFFFFFFF1
   c. 从向量表读取 handler 地址：
      vector_table_base + 4 * (irq_n + 16)
   d. 设置 PC = handler 地址（bit[0] 必须为 1，Thumb mode）
   e. 进入 Handler 模式
4. CPU 继续 step()，执行中断 handler
```

### D3C-3：中断返回

BX 指令和 POP {PC} 都会触发 EXC_RETURN 检测。
两者共享 `write_pc()` 路径，检测逻辑只写一处，不留缺口。

EXC_RETURN 魔术值：
```
0xFFFFFFF1 → 返回 Handler mode（嵌套中断）
0xFFFFFFF9 → 返回 Thread mode（主程序）
0xFFFFFFFD → 返回 Thread mode，使用 PSP（不实现，只用 MSP）
```

处理：
1. 从栈弹栈恢复：{R0, R1, R2, R3, R12, LR, PC, xPSR}
2. 退出 Handler 模式
3. PC 设为弹出的 PC 值（bit[0] 清零后为实际地址，bit[0]=1 表示 Thumb）

### D3C-4：SysTick 外设

SysTick 通过 `SimulationCoordinator` 注册到 Sysclk 时钟域，接收域本地时钟周期。

```cpp
class SysTickPeripheral : public periph::Device {
    uint32_t ctrl_ = 0;
    uint32_t load_ = 0;
    uint32_t val_  = 0;
    NvicPeripheral& nvic_;

public:
    SysTickPeripheral(NvicPeripheral& nvic);

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void>   write(addr_t offset, data_t val, Width w) override;
    void tick(uint64_t ticks) override;  // 由 SimulationCoordinator 调用
    std::string_view name() const noexcept override { return "SysTick"; }
};
```

**接线**：芯片配置层将 SysTick 注册到 coordinator：
```cpp
coordinator.add_tickable(systick.GetWeak(),
                         domain_index(ClockDomain::Sysclk));
```

**tick() 逻辑**（while 循环防止跳零）：
```
if (ctrl_ & 0x1) {          // ENABLE bit
    while (cycles > 0) {
        if (val_ > cycles) {
            val_ -= cycles;
            break;
        }
        // 归零触发
        cycles -= val_;
        ctrl_ |= 0x10000;   // COUNTFLAG
        val_ = load_;        // 自动重载
        if (ctrl_ & 0x2) {   // TICKINT bit
            nvic_.set_pending(15);  // SysTick IRQ = 15
        }
    }
}
```

### D3C-5：向量表地址

STM32F103 的向量表默认在 0x08000000（Flash 起始）。
VTOR（Vector Table Offset Register，在 SCB 中）可以重定位。
本 Phase 先固定为 0x08000000，VTOR 支持作为可选扩展。

### D3C-6：HardFault 和 NMI

```cpp
// HardFault：未定义指令 / 总线错误 / 其他 fault
// NMI：不可屏蔽中断
// 两者都通过向量表跳转，编号固定：
//   NMI = 2  → 向量表偏移 0x08
//   HardFault = 3 → 向量表偏移 0x0C
```

第一版处理：
- 未定义指令 → 触发 HardFault
- MemoryBus 返回 BusError → 触发 HardFault
- NMI 可手动触发（调试用）

### D3C-7：0xE000_0000 地址范围映射

NVIC（0xE000_E100）、SysTick（0xE000_E010）、SCB（0xE000_ED00）
都在 0xE000_0000 范围内。

**映射方式**：
- 创建一个 `CortexM3Internal` FlatMemory 或独立外设
- 在 STM32F103 地址映射中添加此范围
- 或者：让这些外设直接挂在 MemoryBus 上

推荐直接挂在 MemoryBus 上，每个外设映射自己的寄存器范围。

---

## 已决定事项

| # | 问题 | 决定 |
|---|------|------|
| T3C-1 | NVIC ↔ CPU 循环引用 | **已解决**：NVIC 不持有 CPU&，采用 pull 模式——CPU 每步查询 NVIC，NVIC 无需反向调用 CPU。循环引用消失，无构造顺序问题。 |
| T3C-2 | 中断检查时机 | 本 Phase 实施时决定（建议 step() 开头，coordinator 先 tick 再下次 step） |
| T3C-3 | 单级 vs 嵌套中断 | **第一版单级**，`current_priority_` 字段已预留，后续可扩展嵌套抢占。 |
| T3C-4 | VTOR 是否实现？ | 本 Phase 结束后评估 |

| # | 问题 | 决定 |
|---|------|------|
| T3C-5 | EXC_RETURN 检测范围 | **BX LR 和 POP {PC} 都检测**，统一走 `write_pc()` 函数，不留 TODO 缺口。 |
| T3C-6 | SysTick tick 逻辑 | **while 循环**处理跳零，不使用简单减法。 |

---

## 隐藏风险

### R3C-1：压栈顺序（最高风险）
必须按 ARM 规范：xPSR, PC, LR, R12, R3, R2, R1, R0。
压栈从高地址到低地址。如果顺序错，弹栈后所有寄存器值混乱，程序行为不可预测。
**应对**：写专门的单元测试验证压栈/弹栈的内存布局。

### R3C-2：EXC_RETURN 检测位置
BX LR 和 POP {PC} 都必须检测 EXC_RETURN 值。
通过统一的 `write_pc()` 函数处理，检测逻辑只写一处，避免遗漏。
普通的 BX LR / POP {PC}（值不是 EXC_RETURN）不应触发中断返回。

### R3C-3：NVIC ↔ CPU 循环引用（已解决）
采用 pull 模式：CPU 持有 NVIC& 用于查询，NVIC 不持有 CPU&。
循环引用不存在，无需两阶段构造。

### R3C-4：SysTick 周期精度（已解决）
使用 while 循环代替简单减法，正确处理 `val_ < ticks` 的情况。
即使 ticks 很大或连续多次归零，逻辑也能正确处理。

---

## 测试计划

### test_nvic.cpp
- 写 ISER 使能 IRQ → is_enabled() 返回 true
- 写 ISPR 触发 pending → has_pending_irq() 返回 true
- 写 ICPR 清除 pending → has_pending_irq() 返回 false
- 最高优先级查询正确

### test_systick.cpp
- 设置 CTRL=1, LOAD=100 → tick(100) → COUNTFLAG 置位
- TICKINT=1 → tick(100) 后 NVIC 中 SysTick IRQ pending
- 连续 tick → 多次归零

### test_interrupt_roundtrip.cpp（集成测试）
1. 配置向量表：entry[0]=SP, entry[1]=Reset, entry[16]=SysTick_Handler（irq 0）
2. 配置 SysTick：LOAD=10, CTRL=3（ENABLE + TICKINT）
3. 通过 SimulationCoordinator 运行 10 步 → SysTick 触发 → handler 被调用
4. handler 中执行 BX LR（EXC_RETURN）→ 返回主程序
5. 验证：主程序 PC 恢复正确，寄存器恢复正确
6. 同上，handler 用 POP {PC}（EXC_RETURN）返回 → 同样验证恢复正确

## 验收标准

- [ ] NVIC 寄存器读写测试通过
- [ ] 中断使能/pending/clear 逻辑正确
- [ ] 中断响应：压栈 8 寄存器 + 跳转向量正确
- [ ] 中断返回：BX LR / POP {PC} 检测 EXC_RETURN → 弹栈恢复正确
- [ ] 返回后 LR = EXC_RETURN 值（0xFFFFFFF9 for Thread mode）
- [ ] SysTick 按 cycles 递减，归零时触发中断
- [ ] HardFault 在未定义指令时触发
- [ ] 中断往返集成测试通过
- [ ] 所有测试 ctest 绿色
