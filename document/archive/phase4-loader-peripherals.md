# Phase 4 · 加载器 + 外设

> 预计工期：2-3 周 | 依赖：Phase 3B | 状态：已完成（核心部分）

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

### D4-7：Timer 基础（含预分频）

```cpp
class TimerPeripheral : public IPeripheral {
    uint32_t cr1_ = 0;     // Control Register 1
    uint32_t arr_ = 0;     // Auto-Reload Register
    uint32_t cnt_ = 0;     // Counter
    uint32_t psc_ = 0;     // Prescaler
    uint32_t sr_  = 0;     // Status Register

public:
    void tick(uint64_t cycles) override;  // 推进 CNT
    // tick 逻辑（含 PSC 预分频）：
    //   if (cr1_ & 0x1) {  // CEN: Counter Enable
    //       cnt_ += cycles / (psc_ + 1);
    //       if (cnt_ >= arr_) {
    //           sr_ |= 0x1;  // UIF: Update Interrupt Flag
    //           cnt_ = 0;
    //       }
    //   }
    // 注意：UIF 中断挂 NVIC 是 Phase 5 的事，Phase 4 只做标志位置位
};
```

**PSC 处理**：`psc_ + 1` 是真实 STM32 行为——PSC=0 表示 1 分频（不分频），
PSC=7199 表示 7200 分频。不实现 PSC 的话 Timer 行为与真实芯片严重不符。

### D4-8：SoC 抽象 — Machine + Stm32f103Soc（方案 D+）

**设计决策**：采用跨 ISA 分层设计（详见 `document/notes/004-soc-board-abstraction-research.md` 方案 D+）。
将原来的 `configure_stm32f103()` 自由函数替换为三层结构：

1. **`chips::Machine`** — 芯片无关的运行骨架
2. **`stm32f1::Stm32f103Parts`** — STM32F103 外设集合（透明 struct）
3. **`stm32f1::Stm32f103Soc`** — 组装层，持有 Machine + Parts

```cpp
// chips::Machine — 通用运行时，不依赖任何具体芯片或 ISA
struct Machine {
    memory::Bus bus;
    std::unique_ptr<cpu::CPU> cpu;
    sim::SimulationCoordinator coord;

    Expected<void> reset();
    Expected<void> load_bin(addr_t base, std::span<const uint8_t> data);
    Expected<void> load_elf(std::span<const uint8_t> data);
    void run(size_t max_steps);

    Machine(const Machine&) = delete;
    Machine(Machine&&) = delete;
};
```

```cpp
// stm32f1::Stm32f103Parts — 外设集合，透明可访问
struct Stm32f103Parts {
    memory::FlatMemory flash{128 * 1024};
    memory::FlatMemory sram{20 * 1024};

    periph::NvicPeripheral nvic;
    periph::SysTickPeripheral systick;

    periph::RccPeripheral rcc;
    periph::GpioPeripheral gpioa{'A'};
    periph::GpioPeripheral gpiob{'B'};
    periph::GpioPeripheral gpioc{'C'};
    periph::UsartPeripheral usart1;
    periph::TimerPeripheral tim2;

    periph::GpioPeripheral& gpio(char id);
};
```

```cpp
// stm32f1::Stm32f103Soc — 组装层
class Stm32f103Soc {
public:
    static Expected<std::unique_ptr<Stm32f103Soc>> create();

    chips::Machine& machine() { return machine_; }
    Stm32f103Parts& parts()   { return parts_; }

    Expected<void> reset();
    Expected<void> load_elf(std::span<const uint8_t> data);
    Expected<void> load_bin(addr_t base, std::span<const uint8_t> data);
    void run(size_t max_steps);

    // 不可移动/拷贝（WeakPtrFactory 限制）
    Stm32f103Soc(const Stm32f103Soc&) = delete;
    Stm32f103Soc(Stm32f103Soc&&) = delete;

private:
    Stm32f103Soc();
    chips::Machine machine_;
    Stm32f103Parts parts_;
};
```

**`Stm32f103Soc::create()` 内部完成所有连线**：映射内存区域、挂载外设到 Bus、
创建 CPU 并接入 NVIC、配置 SimulationCoordinator 的 tickable 和 clock domain。

**使用方式**：
```cpp
auto soc = stm32f1::Stm32f103Soc::create();
(*soc)->load_elf(firmware);
(*soc)->reset();
(*soc)->run(100'000);

// 测试中访问具体外设
auto& parts = (*soc)->parts();
parts.nvic.set_pending(15);
```

**过渡期策略**：
- Phase 4 保留 `CortexM3CPU::set_nvic()`，由 `Stm32f103Soc::create()` 调用
- 后续引入 `cpu::InterruptController` 接口（Phase 5+），解除 CPU 对具体 NVIC 类型的依赖
- `InterruptController` 接口在 Phase 4 先声明（头文件 only），不接入

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T4-1 | ELF 加载器是否需要处理重定位？裸机固件通常不需要。 | **决定：不处理** |
| T4-2 | GPIO 是否只实现 GPIOA-E 中的部分？ | 视验收程序需要 |
| T4-3 | Timer 中断？ | **决定：CNT + PSC + UIF，中断挂 NVIC 留 Phase 5** |
| T4-4 | 固件编译工具链：arm-none-eabi-gcc 版本和链接脚本 | 本 Phase 实施时 |
| T4-5 | SoC/Board 抽象方案选择 | **决定：方案 D+（跨 ISA 分层）**，详见 004-soc-board-abstraction-research.md |

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

- [x] BIN 加载器：裸二进制写入 Flash，PC 从正确地址执行
- [x] ELF32 加载器：PT_LOAD 段正确加载到对应地址
- [x] Reset 序列：向量表[0]→SP，向量表[1]→PC，与真实 Cortex-M3 行为一致
- [x] RCC 寄存器可读写
- [x] GPIO：写 ODR/BSRR → 终端输出 `[GPIO] GPIOA.PINx → HIGH/LOW`
- [x] USART：写 DR → stdout 输出字符
- [x] USART：SR.TXE 和 SR.TC 始终为 1
- [x] Timer：CNT 按 cycles 递增，含 PSC 预分频，溢出设 UIF
- [x] 内存转储工具可用
- [x] MMIO 跟踪工具可用
- [x] **端到端验证**：用 arm-none-eabi-gcc 编译最小 Hello World 固件 → ELF 加载 → reset → run → 验证 stdout 输出 "Hello"
- [x] **端到端验证**：用 arm-none-eabi-gcc 编译 GPIO 翻转固件 → 加载运行 → 验证 GPIO 日志输出
- [x] 所有测试 ctest 绿色
