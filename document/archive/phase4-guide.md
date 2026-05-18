# Phase 4 实施手册 · 加载器 + 外设

> 状态：待实施 | 依赖：Phase 3B | 参考：`document/milestones/phase4-loader-peripherals.md`

本文档按实施顺序组织，每个模块给出完整头文件接口 + 实现思路 + 测试建议。
**不包含实现代码**——那是你的任务。

---

## 实施顺序

```
1. BinLoader           → 最简单，验证 MemoryBus::write 路径
2. ElfLoader           → 在 BinLoader 基础上加 ELF 解析
3. Machine             → 通用运行时骨架（新文件）
4. RccPeripheral       → 最简单的 MMIO 外设，验证外设模式
5. GpioPeripheral      → MMIO + 副作用（日志输出）
6. UsartPeripheral     → MMIO + stdout 输出
7. TimerPeripheral     → MMIO + tick() 时序
8. Stm32f103Parts/Soc  → 组装一切
9. Reset 序列          → 向量表读取 + CPU 启动
10. 端到端验证          → arm-none-eabi-gcc 固件
```

---

## 1. BinLoader

### 文件

- `include/loader/bin_loader.hpp`
- `src/loader/bin_loader.cpp`
- `test/test_bin_loader.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace micro_forge::loader {

struct LoadResult {
    uint32_t entry_point;  // 对于 BIN 就是 base_addr
};

std::expected<LoadResult, std::string> load_bin(
    memory::Bus& bus,
    uint32_t base_addr,
    std::span<const uint8_t> data);

} // namespace micro_forge::loader
```

### 实现思路

1. `load_bin` 从 `base_addr` 开始，每次写一个 Word（4 字节）到 Bus
2. 用 `bus.write(addr, value, Width::Word)` 逐字写入
3. 注意：最后一个 chunk 可能不足 4 字节，需要处理尾部
4. 如果 `bus.write` 返回错误（如 Unmapped），返回错误字符串
5. 返回 `LoadResult{.entry_point = base_addr}`

### 测试建议

```
TEST(BinLoader, LoadToFlash)
  - 创建 Bus + FlatMemory(128KB)，映射到 0x08000000
  - 写入 16 字节数据到 0x08000000
  - 读回验证每个 Word 正确
  - 验证返回的 entry_point == 0x08000000

TEST(BinLoader, UnmappedAddress)
  - 不映射任何内存
  - 调用 load_bin → 应返回错误

TEST(BinLoader, UnalignedSize)
  - 写入 7 字节数据（非 4 对齐）
  - 验证前 4 字节正确，后 3 字节也正确写入
```

---

## 2. ElfLoader

### 文件

- `include/loader/elf_loader.hpp`
- `src/loader/elf_loader.cpp`
- `test/test_elf_loader.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace micro_forge::loader {

struct ElfLoadResult {
    uint32_t entry_point;
};

std::expected<ElfLoadResult, std::string> load_elf(
    memory::Bus& bus,
    std::span<const uint8_t> elf_data);

} // namespace micro_forge::loader
```

### 实现思路

你需要手动解析 ELF32 格式。不要引入外部库（如 libelf），结构体定义自己写。

**ELF32 结构体定义**（放在 `.cpp` 内部或匿名命名空间）：

```cpp
// ELF32 魔数和类型常量
static constexpr uint8_t ELF_MAGIC[4] = {0x7F, 'E', 'L', 'F'};
static constexpr uint8_t ELFCLASS32 = 1;
static constexpr uint8_t ELFDATA2LSB = 1;  // Little-endian
static constexpr uint8_t ET_EXEC = 2;
static constexpr uint8_t EM_ARM = 40;
static constexpr uint32_t PT_LOAD = 1;

// ELF32 Header (52 bytes)
struct Elf32_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;      // 入口地址
    uint32_t e_phoff;      // Program header table offset
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;     // ELF header size = 52
    uint16_t e_phentsize;  // Program header entry size = 32
    uint16_t e_phnum;      // Number of program headers
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

// Program Header (32 bytes)
struct Elf32_Phdr {
    uint32_t p_type;    // 段类型，找 PT_LOAD
    uint32_t p_offset;  // 段数据在文件中的偏移
    uint32_t p_vaddr;   // 虚拟地址（裸机 = 物理地址）
    uint32_t p_paddr;   // 物理地址
    uint32_t p_filesz;  // 段在文件中的大小
    uint32_t p_memsz;   // 段在内存中的大小
    uint32_t p_flags;
    uint32_t p_align;
};
```

**解析步骤**：

1. **验证 ELF 头**：
   - `e_ident[0..3]` == `{0x7F, 'E', 'L', 'F'}`
   - `e_ident[4]` == `ELFCLASS32`（32 位）
   - `e_ident[5]` == `ELFDATA2LSB`（小端）
   - `e_machine` == `EM_ARM`
   - 任何不匹配 → 返回错误字符串

2. **遍历 Program Headers**：
   - 从 `elf_data[e_phoff]` 开始，共 `e_phnum` 个，每个 `e_phentsize` 字节
   - 只处理 `p_type == PT_LOAD` 的段
   - 对每个 PT_LOAD 段：
     - 源数据 = `elf_data[p_offset .. p_offset + p_filesz]`
     - 目标地址 = `p_paddr`（裸机固件用物理地址）
     - 逐 Word 写入 Bus（复用 BinLoader 的写入逻辑，可提取内部 helper）
   - 如果 `p_memsz > p_filesz`，多出的部分填零（BSS 段）

3. **返回** `ElfLoadResult{.entry_point = e_entry}`

**注意**：所有结构体用 `static_assert(sizeof(Elf32_Ehdr) == 52)` 等断言验证大小。
解析时用 `memcpy` 从 span 中读取，不要直接强转指针（对齐问题）。

### 测试建议

手工构造一个最小 ELF 文件（在测试中用 `std::vector<uint8_t>` 拼装）：

```
TEST(ElfLoader, MinimalValidElf)
  - 构造：Elf32_Ehdr + 一个 PT_LOAD 段指向测试数据
  - 加载后验证数据被写入正确地址
  - 验证 entry_point 正确

TEST(ElfLoader, InvalidMagic)
  - 魔数错误 → 返回错误

TEST(ElfLoader, Not32Bit)
  - e_ident[4] != 1 → 返回错误

TEST(ElfLoader, NotArm)
  - e_machine != 40 → 返回错误

TEST(ElfLoader, MultipleLoadSegments)
  - 两个 PT_LOAD 段（模拟 .text + .data）
  - 验证两个地址范围都有正确数据

TEST(ElfLoader, BssZeroFill)
  - p_memsz > p_filesz 的段
  - 验证 filesz 之后的部分被填零
```

---

## 3. Machine（通用运行时骨架）

### 文件

- `include/chips/machine.hpp`
- `src/chips/machine.cpp`（如果实现需要 .cpp；如果全是 inline 可以 header-only）

### 接口

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "core/types.hpp"
#include "memory/bus.hpp"
#include "cpu/cpu.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"

namespace micro_forge::chips {

struct Machine {
    std::unique_ptr<memory::Bus> bus;
    std::unique_ptr<cpu::CPU> cpu;
    std::unique_ptr<sim::SimulationCoordinator> coord;

    // 便捷操作（内部调用 cpu/coordinator）
    std::expected<void, std::string> load_bin(
        uint32_t base, std::span<const uint8_t> data);

    std::expected<void, std::string> load_elf(
        std::span<const uint8_t> data);

    void run(size_t max_steps = SIZE_MAX);

    // 不可移动/拷贝（内部含 WeakPtrFactory 的组件通过 unique_ptr 间接持有）
    Machine() = default;
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;
};

} // namespace micro_forge::chips
```

### 实现思路

- `load_bin`：调用 `loader::load_bin(bus, base, data)`，转发错误
- `load_elf`：调用 `loader::load_elf(bus, data)`，转发错误，保存 entry_point
- `run`：调用 `coord.run(max_steps)`
- `Machine` 本身只是一个组合容器，不持有任何 WeakPtrFactory 对象
- 所有有 WeakPtrFactory 的对象（Bus, 外设）通过其他方式管理

**注意**：Machine 不持有 SimulationCoordinator 的 clock 构造参数。
在 Stm32f103Soc::create() 中，先构造 VirtualClock，再构造 Coordinator，
再赋值到 Machine 的 coord 字段。这需要 SimulationCoordinator 支持移动构造或
在 create() 中用 placement new。查看现有 Coordinator 是否可移动。

---

## 4. RccPeripheral

### 文件

- `include/periph/rcc.hpp`
- `src/periph/rcc.cpp`
- `test/test_rcc.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include "core/types.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

namespace micro_forge::periph {

class RccPeripheral : public Device {
public:
    RccPeripheral() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "RCC"; }

    bool is_clock_enabled(uint32_t peripheral_addr) const;

    WeakPtr<RccPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    // STM32F103 RCC 寄存器（只列出 Phase 4 需要的）
    uint32_t cr_      = 0x00000083;  // Clock Control: HSI on + HSIRDY
    uint32_t cfgr_    = 0x00000000;  // Clock Configuration
    uint32_t cir_     = 0x00000000;  // Clock Interrupt
    uint32_t apb1enr_ = 0x00000000;  // APB1 peripheral clock enable
    uint32_t apb2enr_ = 0x00000000;  // APB2 peripheral clock enable
    uint32_t ahbenr_  = 0x00000000;  // AHB peripheral clock enable

    WeakPtrFactory<RccPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
```

### 实现思路

**寄存器偏移映射**（基址 0x40021000）：

| 偏移 | 寄存器 | 复位值 |
|------|--------|--------|
| 0x00 | CR | 0x00000083 |
| 0x04 | CFGR | 0x00000000 |
| 0x08 | CIR | 0x00000000 |
| 0x14 | AHBENR | 0x00000000 |
| 0x18 | APB2ENR | 0x00000000 |
| 0x1C | APB1ENR | 0x00000000 |

**read**：
- 用 switch(offset) 分发到对应寄存器
- 未知偏移 → `std::unexpected(BusError::Fault)`
- 宽度检查：只支持 Width::Word

**write**：
- 同样 switch 分发
- 直接写入对应成员变量
- Phase 4 不需要实际改变时钟行为

**is_clock_enabled**：
- 根据传入的外设地址判断属于哪个总线（APB1/APB2/AHB）
- 检查对应 enable register 的对应 bit
- 地址到 bit 的映射表（简化版）：

```
APB2ENR bit 2: GPIOA (0x40010800)
APB2ENR bit 3: GPIOB (0x40010C00)
APB2ENR bit 4: GPIOC (0x40011000)
APB2ENR bit 14: USART1 (0x40013800)
APB1ENR bit 0: TIM2 (0x40000000)
```

Phase 4 简化策略：`is_clock_enabled` 可以先返回 true（始终使能）。
后续加日志：访问未使能的外设时打印警告。

### 测试建议

```
TEST(Rcc, ReadResetValues)
  - 创建 RccPeripheral
  - 读 CR (offset 0x00) → 0x00000083
  - 读 APB2ENR (offset 0x18) → 0x00000000

TEST(Rcc, WriteApb2Enable)
  - 写 APB2ENR = 0x00000004 (enable GPIOA clock)
  - 读回 APB2ENR → 0x00000004

TEST(Rcc, MmioThroughBus)
  - 创建 Bus + map RCC 到 0x40021000
  - 通过 bus.write(0x40021018, 0x4, Width::Word)
  - 通过 bus.read(0x40021018) 验证

TEST(Rcc, InvalidOffset)
  - 读/写未定义偏移 → BusError::Fault
```

---

## 5. GpioPeripheral

### 文件

- `include/periph/gpio.hpp`
- `src/periph/gpio.cpp`
- `test/test_gpio.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include "core/types.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

namespace micro_forge::periph {

class GpioPeripheral : public Device {
public:
    explicit GpioPeripheral(uint8_t port_id);

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override;

    // 测试辅助：直接设置 IDR（模拟外部输入）
    void simulate_input(uint8_t pin, bool high);

    WeakPtr<GpioPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    void on_odr_changed(uint32_t old_odr, uint32_t new_odr);

    uint32_t crl_  = 0x44444444;   // Config Register Low (pin 0-7)
    uint32_t crh_  = 0x44444444;   // Config Register High (pin 8-15)
    uint32_t idr_  = 0;            // Input Data Register (read-only)
    uint32_t odr_  = 0;            // Output Data Register
    uint32_t lckr_ = 0;            // Lock Register
    uint8_t port_id_;              // 'A', 'B', 'C', 'D', 'E'

    WeakPtrFactory<GpioPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
```

### 实现思路

**寄存器偏移映射**（基址视端口而定）：

| 偏移 | 寄存器 | 属性 |
|------|--------|------|
| 0x00 | CRL | 读/写 |
| 0x04 | CRH | 读/写 |
| 0x08 | IDR | 只读 |
| 0x0C | ODR | 读/写 |
| 0x10 | BSRR | 只写 |
| 0x14 | BRR | 只写 |
| 0x18 | LCKR | 读/写 |

**关键行为**：

**写 ODR（offset 0x0C）**：
1. 保存旧值 `old_odr = odr_`
2. 写入新值 `odr_ = data`
3. 调用 `on_odr_changed(old_odr, odr_)`

**写 BSRR（offset 0x10）**——这是最容易出错的：
1. 低 16 位（bit 0-15）：写 1 置位对应 ODR bit
2. 高 16 位（bit 16-31）：写 1 清零对应 ODR bit
3. 实现：
   ```
   old_odr = odr_;
   set_bits   = data & 0xFFFF;        // 低 16 位
   reset_bits = (data >> 16) & 0xFFFF; // 高 16 位
   odr_ = (odr_ | set_bits) & ~reset_bits;
   on_odr_changed(old_odr, odr_);
   ```
4. BSRR 是只写的——读 offset 0x10 返回 `BusError::Fault`（或默认值 0）

**写 BRR（offset 0x14）**：
1. 低 16 位写 1 清零对应 ODR bit
2. `odr_ &= ~(data & 0xFFFF);`

**读 IDR（offset 0x08）**：
- 返回 `idr_`（默认 0，可通过 `simulate_input` 设置）

**on_odr_changed**：
- 遍历 bit 0-15
- 如果 `old_odr` 的 bit N != `new_odr` 的 bit N
- 打印日志：`[GPIO] GPIO{port_id_}.PIN{N} → HIGH/LOW`
- 使用 `util/logger.hpp` 或直接 `fprintf(stderr, ...)`

### 测试建议

```
TEST(Gpio, WriteOdrLogsChange)
  - 创建 GPIOA
  - 写 ODR = 0x0020 (pin 5 high)
  - 验证 ODR 读回 0x0020
  - 捕获日志输出包含 "GPIOA.PIN5 → HIGH"

TEST(Gpio, BsrrSetAndReset)
  - 写 BSRR = 0x0020 (set pin 5)
  - 验证 ODR bit 5 == 1
  - 写 BSRR = 0x00200000 (reset pin 5)
  - 验证 ODR bit 5 == 0

TEST(Gpio, BsrrReadOnly)
  - 读 offset 0x10 → 应报错

TEST(Gpio, IdrReadOnly)
  - 写 IDR (offset 0x08) → 应报错或忽略
  - 读 IDR → 返回 0

TEST(Gpio, SimulateInput)
  - simulate_input(3, true)
  - 读 IDR → bit 3 == 1

TEST(Gpio, MmioThroughBus)
  - 创建 Bus + map GPIOA 到 0x40010800
  - 通过 bus 写 BSRR + 读 ODR

TEST(Gpio, MultiplePorts)
  - 创建 GPIOA, GPIOB, GPIOC
  - 各自映射到不同地址
  - 操作互不干扰
```

---

## 6. UsartPeripheral

### 文件

- `include/periph/usart.hpp`
- `src/periph/usart.cpp`
- `test/test_usart.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include "core/types.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

namespace micro_forge::periph {

class UsartPeripheral : public Device {
public:
    UsartPeripheral() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "USART"; }

    // 注入输出后端（默认 stdout，测试时可替换）
    void set_output(std::function<void(uint8_t)> fn);

    WeakPtr<UsartPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    uint32_t sr_   = 0x000000C0;   // Status: TXE=1, TC=1
    uint32_t dr_   = 0;
    uint32_t brr_  = 0;
    uint32_t cr1_  = 0;
    uint32_t cr2_  = 0;
    uint32_t cr3_  = 0;

    std::function<void(uint8_t)> output_;

    WeakPtrFactory<UsartPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
```

### 实现思路

**寄存器偏移映射**（基址视 USART 编号而定）：

| 偏移 | 寄存器 | 属性 |
|------|--------|------|
| 0x00 | SR | 读/写（部分位写 0 清零） |
| 0x04 | DR | 读/写 |
| 0x08 | BRR | 读/写 |
| 0x0C | CR1 | 读/写 |
| 0x10 | CR2 | 读/写 |
| 0x14 | CR3 | 读/写 |

**关键行为**：

**写 DR（offset 0x04）**：
1. `dr_ = data`
2. 取低 8 位：`ch = data & 0xFF`
3. 调用输出函数：`output_ ? output_(ch) : putchar(ch)`
4. **不改变 SR**（TXE/TC 始终为 1）

**读 DR（offset 0x04）**：
- 返回 `dr_`
- Phase 4 不模拟接收，DR 读回上次写入的值

**读 SR（offset 0x00）**：
- 返回 `sr_`（初始值 0xC0 = bit7 TXE + bit6 TC）
- 固件通过轮询 `SR & 0x80`（TXE）来判断是否可以发送
- 因为 TXE 始终为 1，轮询不会卡死

**set_output**：
- 允许测试注入自定义输出函数
- 例如：写入 `std::vector<uint8_t>` 缓冲区，用于断言输出内容

### 测试建议

```
TEST(Usart, WriteDrOutputsChar)
  - 设置 output_ 捕获器
  - 写 DR = 'H' (0x48)
  - 验证捕获器收到 'H'

TEST(Usart, SrTxeAlwaysHigh)
  - 读 SR → (sr & 0x80) != 0

TEST(Usart, SrTcAlwaysHigh)
  - 读 SR → (sr & 0x40) != 0

TEST(Usart, WriteCr1)
  - 写 CR1 = 0x000C (TE + RE enabled)
  - 读 CR1 → 0x000C

TEST(Usart, MmioThroughBus)
  - map USART1 到 0x40013800
  - 通过 bus 写 DR → 验证输出

TEST(Usart, PollingLoop)
  - 模拟固件的轮询发送模式：
    while (!(read_sr() & 0x80)) {}  // 不会死循环
    write_dr('A');
  - 验证输出 'A'
```

---

## 7. TimerPeripheral

### 文件

- `include/periph/timer.hpp`
- `src/periph/timer.cpp`
- `test/test_timer.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include "core/types.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

namespace micro_forge::periph {

class TimerPeripheral : public Device {
public:
    TimerPeripheral() = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    void tick(uint64_t cycles) override;
    std::string_view name() const noexcept override { return "TIM"; }

    WeakPtr<TimerPeripheral> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    uint32_t cr1_ = 0;     // Control Register 1
    uint32_t dier_ = 0;    // DMA/Interrupt Enable Register
    uint32_t arr_ = 0;     // Auto-Reload Register
    uint32_t cnt_ = 0;     // Counter
    uint32_t psc_ = 0;     // Prescaler
    uint32_t sr_  = 0;     // Status Register

    WeakPtrFactory<TimerPeripheral> weak_factory_{this};
};

} // namespace micro_forge::periph
```

### 实现思路

**寄存器偏移映射**（基址视 TIM 编号而定）：

| 偏移 | 寄存器 |
|------|--------|
| 0x00 | CR1 |
| 0x0C | DIER |
| 0x28 | PSC |
| 0x2C | ARR |
| 0x34 | CNT (可写) |
| 0x10 | SR |

**MMIO read/write**：标准 switch 分发。

**tick() 逻辑**（核心）：

```
void tick(uint64_t cycles) {
    if (!(cr1_ & 0x0001))  // CEN (Counter Enable) bit
        return;

    uint64_t prescaled = cycles / (static_cast<uint64_t>(psc_) + 1);
    cnt_ += static_cast<uint32_t>(prescaled);

    if (cnt_ >= arr_) {
        sr_ |= 0x0001;    // UIF: Update Interrupt Flag
        cnt_ = 0;         // 重载
    }
}
```

**注意点**：
- PSC + 1 是真实行为：PSC=0 表示不分频
- `cnt_` 用 uint32_t，溢出是自然截断
- Phase 4 不触发 NVIC 中断（UIF 只是置位，中断是 Phase 5 的事）
- `arr_ = 0` 时的行为：真实芯片会立即溢出。Phase 4 可以不做特殊处理

### 测试建议

```
TEST(Timer, TickIncrements)
  - 设置 arr_ = 100, psc_ = 0
  - 写 CR1 = 0x0001 (CEN)
  - tick(10)
  - 读 CNT → 10

TEST(Timer, Prescaler)
  - 设置 arr_ = 100, psc_ = 9
  - 写 CR1 = 0x0001
  - tick(100)
  - 读 CNT → 10  (100 / (9+1) = 10)

TEST(Timer, OverflowSetsUif)
  - arr_ = 10, psc_ = 0
  - CR1 = 0x0001
  - tick(10)
  - 读 SR → bit 0 == 1 (UIF)
  - 读 CNT → 0

TEST(Timer, DisabledNoTick)
  - CR1 = 0 (CEN not set)
  - tick(100)
  - 读 CNT → 0

TEST(Timer, MmioThroughBus)
  - map TIM2 到 0x40000000
  - 通过 bus 写 CR1, PSC, ARR
  - 通过 bus 读 CNT, SR

TEST(Timer, MultipleOverflows)
  - arr_ = 5, psc_ = 0
  - CR1 = 0x0001
  - tick(12)
  - CNT → 2 (12 % 5 = 2 after two overflows)
  - SR → UIF set
```

---

## 8. Stm32f103Parts + Stm32f103Soc

### 文件

- `include/chips/stm32f1/stm32f103_soc.hpp`
- `src/chips/stm32f1/stm32f103_soc.cpp`
- `test/test_stm32f103_soc.cpp`

### 接口

```cpp
#pragma once
#include <expected>
#include <memory>
#include <span>
#include <string>

#include "chips/machine.hpp"
#include "chips/stm32f1/clock_domains.hpp"
#include "memory/flat_memory.hpp"
#include "periph/nvic.hpp"
#include "periph/systick.hpp"
#include "periph/rcc.hpp"
#include "periph/gpio.hpp"
#include "periph/usart.hpp"
#include "periph/timer.hpp"

namespace micro_forge::chips::stm32f1 {

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

    explicit Stm32f103Parts()
        : systick(nvic) {}

    periph::GpioPeripheral& gpio(char id);
};

class Stm32f103Soc {
public:
    static std::expected<std::unique_ptr<Stm32f103Soc>, std::string> create();

    chips::Machine& machine() { return machine_; }
    Stm32f103Parts& parts()   { return parts_; }

    // 便捷操作
    std::expected<void, std::string> load_elf(std::span<const uint8_t> data);
    std::expected<void, std::string> load_bin(uint32_t base,
                                               std::span<const uint8_t> data);
    void run(size_t max_steps = SIZE_MAX);

    // 禁止移动/拷贝
    Stm32f103Soc(const Stm32f103Soc&) = delete;
    Stm32f103Soc& operator=(const Stm32f103Soc&) = delete;
    Stm32f103Soc(Stm32f103Soc&&) = delete;
    Stm32f103Soc& operator=(Stm32f103Soc&&) = delete;

private:
    Stm32f103Soc() = default;

    chips::Machine machine_;
    Stm32f103Parts parts_;
};

} // namespace micro_forge::chips::stm32f1
```

### 实现思路

**Stm32f103Soc::create()**——这是核心组装函数：

```cpp
static std::expected<std::unique_ptr<Stm32f103Soc>, std::string> Stm32f103Soc::create() {
    // 1. 堆上分配，确保地址稳定（WeakPtrFactory 要求）
    auto soc = std::unique_ptr<Stm32f103Soc>(new Stm32f103Soc());

    auto& bus = soc->machine_.bus;
    auto& c = soc->parts_;

    using namespace micro_forge::literals;

    // 2. 内存映射
    TRY_MSG(bus.map(memory::region(0x0000'0000_addr, 128_kb, c.flash.GetWeak())),
            "failed to map boot alias");
    TRY_MSG(bus.map(memory::region(0x0800'0000_addr, 128_kb, c.flash.GetWeak())),
            "failed to map flash");
    TRY_MSG(bus.map(memory::region(0x2000'0000_addr, 20_kb, c.sram.GetWeak())),
            "failed to map sram");

    // 3. 外设映射（Phase 4 完整列表）
    TRY_MSG(bus.map(memory::region(0x4002'1000_addr, 0x400_addr, c.rcc.GetWeak())),
            "failed to map RCC");
    TRY_MSG(bus.map(memory::region(0x4001'0800_addr, 0x400_addr, c.gpioa.GetWeak())),
            "failed to map GPIOA");
    TRY_MSG(bus.map(memory::region(0x4001'0C00_addr, 0x400_addr, c.gpiob.GetWeak())),
            "failed to map GPIOB");
    TRY_MSG(bus.map(memory::region(0x4001'1000_addr, 0x400_addr, c.gpioc.GetWeak())),
            "failed to map GPIOC");
    TRY_MSG(bus.map(memory::region(0x4001'3800_addr, 0x400_addr, c.usart1.GetWeak())),
            "failed to map USART1");
    TRY_MSG(bus.map(memory::region(0x4000'0000_addr, 0x400_addr, c.tim2.GetWeak())),
            "failed to map TIM2");
    TRY_MSG(bus.map(memory::region(0xE000'E010_addr, 0x010_addr, c.systick.GetWeak())),
            "failed to map SysTick");
    TRY_MSG(bus.map(memory::region(0xE000'E100_addr, 0xC00_addr, c.nvic.GetWeak())),
            "failed to map NVIC");

    // 4. 创建 CPU
    // CortexM3CPU 构造需要 WeakPtr<Bus>
    auto& cpu = soc->machine_.cpu;
    cpu = std::make_unique<cpu::arm::cortex_m3::CortexM3CPU>(bus.GetWeak());
    auto* cm3 = static_cast<cpu::arm::cortex_m3::CortexM3CPU*>(cpu.get());
    cm3->set_nvic(c.nvic);

    // 5. 配置 SimulationCoordinator
    // 需要构造 VirtualClock + Coordinator
    // 查看现有 SimulationCoordinator 的构造函数签名
    // 可能需要在 Machine 中改为 unique_ptr<SimulationCoordinator>
    // 或者 coordinator 支持从外部 move 赋值
    //
    // 大致逻辑：
    //   VirtualClock clk(stm32f103_default_clocks);
    //   coord.set_cpu(cm3->GetWeak());
    //   coord.add_tickable(c.systick.GetWeak(), domain_index(ClockDomain::Sysclk));
    //   coord.add_tickable(c.tim2.GetWeak(), domain_index(ClockDomain::Apb1));

    return soc;
}
```

**注意事项**：

1. `TRY_MSG` 不是标准宏，你需要定义一个辅助宏来把 `BusError` 转为 string
2. `SimulationCoordinator` 的构造需要 `VirtualClock`，需要在 create() 中构造
3. `Stm32f103Parts` 的成员声明顺序很重要：`systick` 构造需要 `nvic` 引用，
   所以 `nvic` 必须在 `systick` 之前声明
4. `Stm32f103Parts` 也不可移动（含 WeakPtrFactory），但作为 `Stm32f103Soc` 的成员
   且 Soc 本身是 `new` 出来的，地址稳定

**关于 SimulationCoordinator 的集成**：

当前 `SimulationCoordinator` 的构造函数接受 `VirtualClock`。
如果 `VirtualClock` 和 `SimulationCoordinator` 都不可移动，
需要考虑以下方案之一：

- 方案 a：Machine 中的 coord 改为 `unique_ptr<SimulationCoordinator>`
- 方案 b：让 SimulationCoordinator 支持从 `VirtualClock&&` 移动构造
- 方案 c：Machine 持有 `optional<VirtualClock>` + `optional<SimulationCoordinator>`

推荐方案 a（最小改动）。具体改动：
```cpp
struct Machine {
    memory::Bus bus;
    std::unique_ptr<cpu::CPU> cpu;
    std::unique_ptr<sim::SimulationCoordinator> coord;  // 改为 unique_ptr

    void run(size_t max_steps) {
        if (coord) coord->run(max_steps);
    }
};
```

### Stm32f103Parts::gpio()

```cpp
periph::GpioPeripheral& Stm32f103Parts::gpio(char id) {
    switch (id) {
        case 'A': return gpioa;
        case 'B': return gpiob;
        case 'C': return gpioc;
        default:  return gpioa;  // 或抛异常
    }
}
```

### 测试建议

```
TEST(Stm32f103Soc, CreateSuccess)
  - create() 成功
  - 验证 parts() 返回的各外设 name() 正确

TEST(Stm32f103Soc, MemoryAccessible)
  - create() 后通过 bus 读写 Flash/SRAM 地址

TEST(Stm32f103Soc, PeripheralsAccessible)
  - 通过 bus 读 RCC 地址 → 0x00000083 (CR 复位值)
  - 通过 bus 写 GPIOA ODR → 读回验证
  - 通过 bus 写 USART1 DR → 验证输出

TEST(Stm32f103Soc, LoadBinAndRun)
  - 构造一段简单的 Thumb 机器码（无限循环：WFI 或 B .）
  - load_bin → reset → run(100) → 不崩溃

TEST(Stm32f103Soc, CannotMove)
  - static_assert(!std::is_move_constructible_v<Stm32f103Soc>)
  - static_assert(!std::is_copy_constructible_v<Stm32f103Soc>)
```

---

## 9. Reset 序列

### 文件

- `include/loader/cortex_m3_reset.hpp`（或放在 Stm32f103Soc 的 reset 方法内）
- `src/loader/cortex_m3_reset.cpp`

### 接口

```cpp
#pragma once
#include <cstdint>
#include <expected>
#include <string>

namespace micro_forge::loader {

std::expected<void, std::string> cortex_m3_reset(
    cpu::arm::cortex_m3::CortexM3CPU& cpu,
    memory::Bus& bus,
    uint32_t vector_table_base = 0x00000000);

} // namespace micro_forge::loader
```

### 实现思路

1. 从 `bus.read(vector_table_base + 0, Width::Word)` 读初始 SP
2. 从 `bus.read(vector_table_base + 4, Width::Word)` 读 Reset_Handler 地址
3. 如果两次 read 都成功：
   - `cpu.reset()`（清零寄存器，设 Thumb bit）
   - `cpu.set_register_value(13, sp)` — SP = R13
   - `cpu.set_pc(pc_value)` — PC
   - `cpu.set_register_value(14, 0xFFFFFFFF)` — LR
4. **PC 的 bit[0] 必须为 1**（Thumb 标志）。ARM Cortex-M 只支持 Thumb 模式。
   如果读回的 PC 值 bit[0] 为 0，应该置 1 并打印警告。
5. Reset 后打印：`[RESET] SP=0x{sp:08X} PC=0x{pc:08X}`

**在 Stm32f103Soc 中的集成**：

```cpp
std::expected<void, std::string> Stm32f103Soc::load_elf(span<const uint8_t> data) {
    auto result = loader::load_elf(machine_.bus, data);
    if (!result) return std::unexpected(result.error());

    auto* cm3 = static_cast<cpu::arm::cortex_m3::CortexM3CPU*>(machine_.cpu.get());

    // reset + 从向量表加载 SP/PC
    TRY(loader::cortex_m3_reset(*cm3, machine_.bus, 0x00000000));

    cm3->launch();  // State::Halted → State::Running
    return {};
}
```

### 测试建议

```
TEST(CortexM3Reset, VectorTableLoad)
  - 构造 Bus + FlatMemory + CortexM3CPU
  - 在 Flash 地址 0 写入：SP=0x20005000, PC=0x08000101
  - 调用 cortex_m3_reset
  - 验证 R13 == 0x20005000
  - 验证 PC == 0x08000101 (含 Thumb bit)

TEST(CortexM3Reset, UnmappedVectorTable)
  - 不映射任何内存
  - cortex_m3_reset → 返回错误

TEST(CortexM3Reset, ThumbBitForced)
  - 向量表 PC 值 bit[0] 为 0
  - 验证 reset 后 PC 的 bit[0] 被置为 1
```

---

## 10. 端到端验证

### 文件

- `test/test_e2e_hello.cpp`（或 `test/firmware/` 子目录）
- `test/firmware/hello_world/`（固件源码和编译脚本）

### 固件编译准备

你需要安装 `arm-none-eabi-gcc` 并准备一个最小链接脚本。

**最小 Hello World 固件**（`test/firmware/hello_world/main.c`）：

```c
// 最小化：不需要标准库
#define USART1_SR   (*((volatile uint32_t*)0x40013800))
#define USART1_DR   (*((volatile uint32_t*)0x40013804))
#define USART1_CR1  (*((volatile uint32_t*)0x4001380C))
#define RCC_APB2ENR (*((volatile uint32_t*)0x40021018))

void usart_init(void) {
    RCC_APB2ENR |= (1 << 14);  // Enable USART1 clock
    RCC_APB2ENR |= (1 << 2);   // Enable GPIOA clock
    USART1_CR1 = 0x000C;       // TE + RE
    USART1_CR1 |= 0x2000;     // UE (USART Enable)
}

void usart_putc(char c) {
    while (!(USART1_SR & 0x80));  // Wait for TXE
    USART1_DR = c;
}

void usart_puts(const char *s) {
    while (*s) usart_putc(*s++);
}

// 向量表
void reset_handler(void);
__attribute__((section(".isr_vector")))
const uint32_t vector_table[] = {
    0x20005000,          // Initial SP (20KB SRAM top)
    (uint32_t)reset_handler,  // Reset Handler
};

void reset_handler(void) {
    usart_init();
    usart_puts("Hello");
    while (1) __asm volatile("nop");
}
```

**链接脚本**（`test/firmware/hello_world/STM32F103.ld`）：

```ld
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text       : { *(.text*) }           > FLASH
    .rodata     : { *(.rodata*) }         > FLASH
}
```

**编译命令**：
```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostdlib -T STM32F103.ld \
    -o hello.elf main.c
arm-none-eabi-objcopy -O binary hello.elf hello.bin
```

### E2E 测试思路

```cpp
TEST(E2E, HelloWorld)
  // 1. 读取预编译的 hello.bin 或 hello.elf
  //    用 std::ifstream 读入 vector<uint8_t>

  // 2. 创建 SoC
  auto soc = Stm32f103Soc::create();
  ASSERT_TRUE(soc.has_value());

  // 3. 捕获 USART 输出
  std::string output;
  (*soc)->parts().usart1.set_output([&](uint8_t ch) {
      output += static_cast<char>(ch);
  });

  // 4. 加载固件
  auto result = (*soc)->load_bin(0x08000000, firmware_data);
  // 或者 load_elf(elf_data)
  ASSERT_TRUE(result.has_value());

  // 5. Reset + 运行
  // ... (load_elf 内部已做 reset)
  (*soc)->run(50'000);  // 足够跑到 "Hello" 输出

  // 6. 验证
  EXPECT_EQ(output, "Hello");
```

### 测试建议

```
E2EHelloWorld:
  - 编译 + 加载 + 运行 → stdout "Hello"

E2EGpioToggle:
  - 固件写 GPIOA ODR/BSRR 翻转 pin 5
  - 验证 GPIO 日志输出

E2ETimerCount:
  - 固件配置 TIM2 (ARR=1000, PSC=0, CEN=1)
  - 运行若干 step
  - 读 TIM2 CNT → 大于 0
```

---

## CMake 集成

新文件会自动被 `file(GLOB_RECURSE MICRO_FORGE_SOURCES src/*.cpp)` 收集。
只需在 `test/CMakeLists.txt` 中添加新的测试可执行文件：

```cmake
# ── 加载器测试 ──
add_executable(test_loader
    test_bin_loader.cpp
    test_elf_loader.cpp
)
target_link_libraries(test_loader
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_loader)

# ── 外设测试（RCC, GPIO, USART, Timer） ──
add_executable(test_periph
    test_rcc.cpp
    test_gpio.cpp
    test_usart.cpp
    test_timer.cpp
)
target_link_libraries(test_periph
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_periph)

# ── SoC 集成测试 ──
add_executable(test_soc
    test_stm32f103_soc.cpp
)
target_link_libraries(test_soc
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_soc)

# ── 端到端验证 ──
add_executable(test_e2e
    test_e2e_hello.cpp
)
target_link_libraries(test_e2e
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_e2e)
```

---

## 文件总览（新增）

```
include/
├── loader/
│   ├── bin_loader.hpp
│   ├── elf_loader.hpp
│   └── cortex_m3_reset.hpp
├── chips/
│   └── machine.hpp
├── chips/stm32f1/
│   └── stm32f103_soc.hpp
├── periph/
│   ├── rcc.hpp
│   ├── gpio.hpp
│   ├── usart.hpp
│   └── timer.hpp

src/
├── loader/
│   ├── bin_loader.cpp
│   ├── elf_loader.cpp
│   └── cortex_m3_reset.cpp
├── chips/
│   └── machine.cpp
├── chips/stm32f1/
│   └── stm32f103_soc.cpp
├── periph/
│   ├── rcc.cpp
│   ├── gpio.cpp
│   ├── usart.cpp
│   └── timer.cpp

test/
├── test_bin_loader.cpp
├── test_elf_loader.cpp
├── test_rcc.cpp
├── test_gpio.cpp
├── test_usart.cpp
├── test_timer.cpp
├── test_stm32f103_soc.cpp
├── test_e2e_hello.cpp
└── firmware/
    └── hello_world/
        ├── main.c
        ├── STM32F103.ld
        └── Makefile
```
