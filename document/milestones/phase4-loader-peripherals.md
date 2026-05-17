# Phase 4 · 加载器 + 外设

> 预计工期：2-3 周 | 依赖：Phase 3B | 状态：待实施

## 目标

能加载真实固件（BIN/ELF），外设模拟支撑 Hello World。
GPIO 翻转有可见输出，USART printf 能在终端显示。

---

## 设计决策

### D4-1：BIN 加载器

```cpp
class BinLoader {
public:
    static std::expected<void, std::string> load(
        MemoryBus& bus,
        uint32_t base_addr,
        std::span<const uint8_t> data);
};
```

直接将二进制数据通过 MemoryBus 写入 base_addr 起始的地址空间。

### D4-2：ELF32 加载器

```cpp
class ElfLoader {
public:
    struct LoadResult {
        uint32_t entry_point;
        // 可选：符号表
    };

    static std::expected<LoadResult, std::string> load(
        MemoryBus& bus,
        std::span<const uint8_t> elf_data);
};
```

解析 ELF32 文件头，遍历 Program Headers，加载 PT_LOAD 段到对应地址。
符号表解析可选（调试辅助）。

**ELF32 需要处理的内容**：
- ELF Header：验证 magic number、确认是 32-bit little-endian ARM
- Program Header：遍历 PT_LOAD 段，提取 p_paddr + p_offset + p_filesz
- 将段数据写入 MemoryBus 对应地址
- 返回入口地址（e_entry）

### D4-3：Reset 序列

```cpp
void cortex_m3_reset(CortexM3Core& cpu, MemoryBus& bus, uint32_t vector_table_base) {
    // 1. 从向量表[0] 读初始 SP
    auto sp = bus.read(vector_table_base + 0, 4);
    cpu.set_reg(13, sp.value());  // SP = R13

    // 2. 从向量表[1] 读初始 PC（Reset_Handler 地址）
    auto pc = bus.read(vector_table_base + 4, 4);
    cpu.set_pc(pc.value());  // PC 的 bit[0] 必须为 1（Thumb）

    // 3. 设置 LR = 0xFFFFFFFF
    cpu.set_reg(14, 0xFFFFFFFF);

    // 4. 清除 xPSR，设置 Thumb bit
    // CortexM3Core::reset() 处理
    cpu.reset();
    cpu.set_reg(13, sp.value());
    cpu.set_pc(pc.value());
}
```

**注意**：PC 值的 bit[0] 必须为 1（Thumb 标志）。实际执行地址是 bit[0] 清零后的值。

### D4-4：RCC 外设

```cpp
class RccPeripheral : public IPeripheral {
    uint32_t cr_ = 0x00000083;      // Clock Control (HSI on)
    uint32_t cfgr_ = 0;             // Clock Configuration
    uint32_t cir_ = 0;              // Clock Interrupt
    uint32_t apb1enr_ = 0;          // APB1 peripheral clock enable
    uint32_t apb2enr_ = 0;          // APB2 peripheral clock enable
    uint32_t ahbenr_ = 0;           // AHB peripheral clock enable

public:
    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    std::string_view name() const override { return "RCC"; }

    // 查询：某外设时钟是否使能（GPIO/USART 等可查询）
    bool is_clock_enabled(uint32_t peripheral_addr) const;
};
```

**简化策略**：RCC 寄存器可读写，但不实际影响时钟频率。所有外设始终可访问。
可选：访问未使能时钟的外设时打印警告日志。

### D4-5：GPIO 外设

```cpp
class GpioPeripheral : public IPeripheral {
    uint32_t crl_  = 0x44444444;   // Configuration Register Low (8 pins)
    uint32_t crh_  = 0x44444444;   // Configuration Register High (8 pins)
    uint32_t idr_  = 0;            // Input Data Register
    uint32_t odr_  = 0;            // Output Data Register
    uint32_t bsrr_ = 0;            // Bit Set/Reset Register
    uint32_t brr_  = 0;            // Bit Reset Register
    uint8_t port_id_;              // 'A', 'B', 'C', 'D', 'E'

public:
    explicit GpioPeripheral(uint8_t port_id);

    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    std::string_view name() const override;
};
```

**关键行为**：
- 写 ODR 或 BSRR 时，检测哪些引脚状态发生了变化
- 变化的引脚打印日志：`[GPIO] GPIOA.PIN5 → HIGH` 或 `[GPIO] GPIOA.PIN5 → LOW`
- 写 IDR 可模拟外部输入（按键、传感器）

### D4-6：USART 外设

```cpp
class UsartPeripheral : public IPeripheral {
    uint32_t sr_   = 0x00C0;       // Status Register (TXE=1, TC=1)
    uint32_t dr_   = 0;            // Data Register
    uint32_t brr_  = 0;            // Baud Rate Register
    uint32_t cr1_  = 0;            // Control Register 1
    uint32_t cr2_  = 0;            // Control Register 2
    uint32_t cr3_  = 0;            // Control Register 3

public:
    std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) override;
    std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) override;
    std::string_view name() const override { return "USART1"; }
};
```

**关键行为**：
- 写 DR → 立即将低 8 位输出到 stdout（`putchar(val & 0xFF)`）
- SR.TXE（bit 7）始终为 1 → 发送缓冲区永远为空
- SR.TC（bit 6）始终为 1 → 发送永远完成
- 这样轮询发送的固件不会死循环

### D4-7：Timer 基础

```cpp
class TimerPeripheral : public IPeripheral {
    uint32_t cr1_ = 0;     // Control Register 1
    uint32_t arr_ = 0;     // Auto-Reload Register
    uint32_t cnt_ = 0;     // Counter
    uint32_t psc_ = 0;     // Prescaler
    uint32_t sr_  = 0;     // Status Register

public:
    void tick(uint64_t cycles) override;  // 推进 CNT
    // tick 逻辑：
    //   if (cr1_ & 0x1) {  // CEN: Counter Enable
    //       cnt_ += cycles;
    //       if (cnt_ >= arr_) {
    //           sr_ |= 0x1;  // UIF: Update Interrupt Flag
    //           cnt_ = 0;
    //       }
    //   }
};
```

### D4-8：STM32F103 完整地址映射

Phase 1 的 `configure_stm32f103()` 在本 Phase 扩展：

```cpp
void configure_stm32f103(MemoryBus& bus,
                          FlatMemory& flash,
                          FlatMemory& sram,
                          RccPeripheral& rcc,
                          GpioPeripheral& gpioa,
                          GpioPeripheral& gpiob,
                          GpioPeripheral& gpioc,
                          UsartPeripheral& usart1,
                          TimerPeripheral& tim2,
                          NvicPeripheral& nvic,
                          SysTickPeripheral& systick);
```

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T4-1 | ELF 加载器是否需要处理重定位？裸机固件通常不需要。 | 本 Phase 实施时（建议不处理） |
| T4-2 | GPIO 是否只实现 GPIOA-E 中的部分？ | 视验收程序需要 |
| T4-3 | Timer 是否需要实现中断？还是只做 CNT 递减？ | 建议：CNT + UIF，中断挂 NVIC |
| T4-4 | 固件编译工具链：arm-none-eabi-gcc 版本和链接脚本 | 本 Phase 实施时 |

---

## 隐藏风险

### R4-1：Reset 序列（最高风险）
如果向量表[0]/[1] 读取的值不正确（比如 ELF 没有正确加载，或向量表偏移不对），
PC 会跳到错误地址，所有后续行为不可预测。
**应对**：Reset 后立即打印 PC 和 SP 值，与真实硬件对比。

### R4-2：ELF 段地址
ARM GCC 生成的 ELF 中，`.text` 段的地址通常是 0x08000000 起始。
如果加载器把段数据写到错误地址，整个程序执行链都会出错。

### R4-3：BSRR 寄存器写行为
BSRR 是「写 1 置位/复位」寄存器：低 16 位写 1 置位对应 ODR bit，高 16 位写 1 清零。
这是只写寄存器，读 BSRR 的行为未定义。GPIO 实现需要正确处理。

---

## 验收标准

- [ ] BIN 加载器：裸二进制写入 Flash，PC 从正确地址执行
- [ ] ELF32 加载器：PT_LOAD 段正确加载到对应地址
- [ ] Reset 序列：向量表[0]→SP，向量表[1]→PC，与真实 Cortex-M3 行为一致
- [ ] RCC 寄存器可读写
- [ ] GPIO：写 ODR/BSRR → 终端输出 `[GPIO] GPIOA.PINx → HIGH/LOW`
- [ ] USART：写 DR → stdout 输出字符
- [ ] USART：SR.TXE 和 SR.TC 始终为 1
- [ ] Timer：CNT 按 cycles 递减，溢出设 UIF
- [ ] 内存转储工具可用
- [ ] MMIO 跟踪工具可用
- [ ] 所有测试 ctest 绿色
