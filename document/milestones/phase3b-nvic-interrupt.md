# Phase 3B · NVIC + SysTick + 中断

> 预计工期：1-2 周 | 依赖：Phase 3A | 状态：待实施

## 目标

实现 Cortex-M3 的中断子系统：NVIC 外设、中断响应（压栈+跳转）、中断返回（弹栈）、SysTick 定时器。
完成后，SysTick 中断能正确触发并返回。

---

## 设计决策

### D3B-1：NVIC 架构（核心设计点）

NVIC 是本项目最特殊的外设——它对内需要与 CPU 深度交互，对外又要是 IPeripheral。

**方案：NVIC 分两层**

```cpp
// 对外：IPeripheral（挂在 MemoryBus 上，处理 MMIO 读写）
class NvicPeripheral : public IPeripheral {
    // NVIC 寄存器状态
    std::array<uint32_t, 8> iser_{};   // Interrupt Set Enable
    std::array<uint32_t, 8> icer_{};   // Interrupt Clear Enable
    std::array<uint32_t, 8> ispr_{};   // Interrupt Set Pending
    std::array<uint32_t, 8> icpr_{};   // Interrupt Clear Pending
    std::array<uint32_t, 60> ip_{};    // Interrupt Priority (8-bit each)

    CortexM3Core& cpu_;  // 回调 CPU 用于触发中断

public:
    explicit NvicPeripheral(CortexM3Core& cpu);

    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    void tick(uint64_t cycles) override;
    std::string_view name() const override { return "NVIC"; }

    // 查询接口（CPU 调用）
    bool has_pending_irq() const;
    uint8_t highest_pending_irq() const;
    uint8_t irq_priority(uint8_t irq_n) const;
    bool is_enabled(uint8_t irq_n) const;
};
```

```cpp
// CPU 内部的中断处理逻辑
class CortexM3Core : public ICore {
    // ... (Phase 3A 内容)

    // 中断处理（CPU 内部）
    void check_and_handle_interrupt();
    void interrupt_entry(uint8_t irq_n);
    void interrupt_return();

    // 状态
    bool in_handler_mode_ = false;
    uint8_t current_priority_ = 0;
};
```

### D3B-2：中断响应流程

```
1. tick() 结束后，CPU 调用 check_and_handle_interrupt()
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

### D3B-3：中断返回

BX 指令检测到 LR 的值为 EXC_RETURN 魔术值时：
```
0xFFFFFFF1 → 返回 Handler mode（嵌套中断）
0xFFFFFFF9 → 返回 Thread mode（主程序）
0xFFFFFFFD → 返回 Thread mode，使用 PSP（不实现，只用 MSP）
0xFFFFFFF1 → 类似
```

处理：
1. 从栈弹栈恢复：{R0, R1, R2, R3, R12, LR, PC, xPSR}
2. 退出 Handler 模式
3. PC 设为弹出的 PC 值（bit[0] 清零后为实际地址，bit[0]=1 表示 Thumb）

### D3B-4：SysTick 外设

```cpp
class SysTickPeripheral : public IPeripheral {
    uint32_t ctrl_ = 0;    // CTRL register
    uint32_t load_ = 0;    // LOAD register (reload value)
    uint32_t val_  = 0;    // VAL register (current value)
    CortexM3Core& cpu_;
    NvicPeripheral& nvic_;

public:
    SysTickPeripheral(CortexM3Core& cpu, NvicPeripheral& nvic);

    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    void tick(uint64_t cycles) override;  // 核心：递减 VAL
    std::string_view name() const override { return "SysTick"; }
};
```

**tick() 逻辑**：
```
if (ctrl_ & 0x1) {          // ENABLE bit
    val_ -= cycles;
    if (val_ == 0) {
        ctrl_ |= 0x10000;   // COUNTFLAG
        val_ = load_;        // 自动重载
        if (ctrl_ & 0x2) {   // TICKINT bit
            nvic_.set_pending(15);  // SysTick IRQ = 15
        }
    }
}
```

### D3B-5：向量表地址

STM32F103 的向量表默认在 0x08000000（Flash 起始）。
VTOR（Vector Table Offset Register，在 SCB 中）可以重定位。
本 Phase 先固定为 0x08000000，VTOR 支持作为可选扩展。

### D3B-6：HardFault 和 NMI

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

### D3B-7：0xE000_0000 地址范围映射

NVIC（0xE000_E100）、SysTick（0xE000_E010）、SCB（0xE000_ED00）
都在 0xE000_0000 范围内。

**映射方式**：
- 创建一个 `CortexM3Internal` FlatMemory 或独立外设
- 在 STM32F103 地址映射中添加此范围
- 或者：让这些外设直接挂在 MemoryBus 上

推荐直接挂在 MemoryBus 上，每个外设映射自己的寄存器范围。

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T3B-1 | NVIC 持有 CPU 引用 vs CPU 持有 NVIC 引用？循环依赖问题。 | 本 Phase 开始时 |
| T3B-2 | 中断检查在 step() 开头还是结尾？ | 本 Phase 实施时（建议结尾，匹配 ARM 流水线行为） |
| T3B-3 | 第一版只做单级中断（不嵌套）是否足够？验收程序不需要嵌套。 | 本 Phase 开始时（建议单级） |
| T3B-4 | VTOR 是否实现？ | 本 Phase 结束后评估 |

---

## 隐藏风险

### R3B-1：压栈顺序（最高风险）
必须按 ARM 规范：xPSR, PC, LR, R12, R3, R2, R1, R0。
压栈从高地址到低地址。如果顺序错，弹栈后所有寄存器值混乱，程序行为不可预测。
**应对**：写专门的单元测试验证压栈/弹栈的内存布局。

### R3B-2：EXC_RETURN 检测位置
BX 指令需要检测 LR 是否为 EXC_RETURN 值（0xFFFFFFF1/0xFFFFFFF9 等）。
这个检测逻辑必须在 BX 的执行路径中，不能遗漏。
同时，普通的 BX LR（LR 不是 EXC_RETURN）不应该触发中断返回。

### R3B-3：NVIC ↔ CPU 循环引用
NVIC 持有 CPU& 用于触发中断，CPU 持有 NVIC& 用于查询 pending。
这是循环引用，但不构成内存问题（两者生命周期相同，都由平台层管理）。

### R3B-4：SysTick 周期精度
val_ -= cycles 的简单递减可能在 cycles 很大时跳过零点。
需要处理 `val_ < cycles` 的情况（归零 + 可能多次触发）。

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
3. 运行 10 步 → SysTick 触发 → handler 被调用
4. handler 中执行 BX LR（EXC_RETURN）→ 返回主程序
5. 验证：主程序 PC 恢复正确，寄存器恢复正确

## 验收标准

- [ ] NVIC 寄存器读写测试通过
- [ ] 中断使能/pending/clear 逻辑正确
- [ ] 中断响应：压栈 8 寄存器 + 跳转向量正确
- [ ] 中断返回：BX LR 检测 EXC_RETURN → 弹栈恢复正确
- [ ] 返回后 LR = EXC_RETURN 值（0xFFFFFFF9 for Thread mode）
- [ ] SysTick 按 cycles 递减，归零时触发中断
- [ ] HardFault 在未定义指令时触发
- [ ] 中断往返集成测试通过
- [ ] 所有测试 ctest 绿色
