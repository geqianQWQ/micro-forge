# SoC/Board 抽象方案调研

> 日期: 2026-05-18
> 阶段: Phase 4
> 状态: 待评审

---

## 问题

Phase 4 的 `configure_stm32f103()` 参数已达 10 个：

```
Bus&, FlatMemory&, FlatMemory&, RccPeripheral&, GpioPeripheral& x3,
UsartPeripheral&, TimerPeripheral&, NvicPeripheral&, SysTickPeripheral&
```

随着外设增多，问题加剧：
1. **组装代码冗长**：每个测试/用例都要手写 30-40 行样板代码
2. **易出错**：漏接一个外设不会报编译错误，运行时才暴露
3. **不可扩展**：加一个外设要改函数签名、所有调用点
4. **生命周期管理困难**：谁拥有外设？栈分配还是堆分配？WeakPtr 悬空风险

需要一个更高层的抽象来封装「创建 + 连线 + 启动」的流程。

更长期的目标不是只支持 STM32F103，而是发展成能够支持多种 MCU 的轻量模拟器：

- Cortex-M 系列（STM32/GD32/NXP 等）
- AVR（如 ATmega328P）
- RISC-V MCU（如 GD32VF103、CH32V 等）
- 8051 派生 MCU
- Xtensa（如 ESP8266/ESP32 系列中的相关核心）

因此本次抽象不能只解决「一个 Board 类怎么写得舒服」；更重要的是避免把
STM32F103/Cortex-M/NVIC 的假设固化进通用层。

---

## 现有架构回顾

```
Device (IPeripheral)
  ├── read(offset, width) → Expected<data_t>
  ├── write(offset, data, width) → Expected<void>
  ├── tick(cycles)
  └── name() → string_view

Bus
  ├── map(Region) → Expected<void>
  ├── read/write(addr, ...) → routes to Region.device
  └── regions_: vector<Region>

Region { start, end, WeakPtr<Device> }

CortexM3CPU
  ├── holds WeakPtr<Bus>
  ├── raw pointer to NvicPeripheral
  └── step() → check_and_handle_interrupt()

SimulationCoordinator
  ├── set_cpu(WeakPtr<CPU>)
  ├── add_tickable(WeakPtr<Device>, domain_index)
  └── step() / run(max_steps)
```

**当前组装模式**（以 test_interrupt_roundtrip.cpp 为例）：
```cpp
FlatMemory flash_{128 * 1024};
FlatMemory sram_{20 * 1024};
Bus bus_;
NvicPeripheral nvic_;
unique_ptr<SysTickPeripheral> systick_;
unique_ptr<CortexM3CPU> cpu_;

configure_memory(bus_, flash_, sram_);
configure_interrupt_devices(bus_, nvic_, *systick_);
cpu_ = make_unique<CortexM3CPU>(bus_.GetWeak());
cpu_->set_nvic(nvic_);
cpu_->reset();
cpu_->launch();
```

Phase 4 加上 RCC、GPIOx3、USART1、TIM2 后，上述代码翻倍。

---

## 方案 A：Monolithic Board Class（单体板级类）

### 设计

```cpp
// include/chips/stm32f1/stm32f103_board.hpp

namespace micro_forge::chips::stm32f1 {

class Stm32f103Board {
public:
    struct Config {
        size_t flash_size = 128 * 1024;
        size_t sram_size = 20 * 1024;
        bool enable_gpio_b = true;
        bool enable_gpio_c = true;
        bool enable_usart1 = true;
        bool enable_tim2 = true;
    };

    static Expected<Stm32f103Board> create(Config cfg = {});

    // 核心操作
    Expected<void> reset();
    Expected<void> load_bin(uint32_t base, span<const uint8_t> data);
    Expected<void> load_elf(span<const uint8_t> data);

    // 组件访问（用于测试 hook、观察输出等）
    CortexM3CPU& cpu()             { return *cpu_; }
    Bus& bus()                     { return bus_; }
    FlatMemory& flash()            { return flash_; }
    FlatMemory& sram()             { return sram_; }
    RccPeripheral& rcc()           { return rcc_; }
    GpioPeripheral& gpio(char id); // 'A','B','C'
    UsartPeripheral& usart(int n); // 1
    TimerPeripheral& timer(int n); // 2
    NvicPeripheral& nvic()         { return nvic_; }
    SysTickPeripheral& systick()   { return systick_; }
    SimulationCoordinator& coord() { return coord_; }

private:
    Stm32f103Board() = default;
    // 全部内含
    FlatMemory flash_;
    FlatMemory sram_;
    Bus bus_;
    RccPeripheral rcc_;
    GpioPeripheral gpioa_{'A'};
    GpioPeripheral gpiob_{'B'};
    GpioPeripheral gpioc_{'C'};
    UsartPeripheral usart1_;
    TimerPeripheral tim2_;
    NvicPeripheral nvic_;
    SysTickPeripheral systick_;
    unique_ptr<CortexM3CPU> cpu_;
    SimulationCoordinator coord_;
};

} // namespace micro_forge::chips::stm32f1
```

### 使用方式

```cpp
auto board = Stm32f103Board::create();
ASSERT_TRUE(board.has_value());

auto& b = board.value();
b.load_elf(firmware_data);
b.reset();

b.coord().run(100'000);
```

### 优点

- **最简使用**：3 行代码启动一个完整系统
- **类型安全**：所有访问器返回具体类型，无类型擦除
- **编译期检查**：漏接外设不可能，构造函数内部完成所有连线
- **所有权清晰**：Board 拥有一切，生命周期由 Board 管理

### 缺点

- **不可定制组合**：不能用 Board 的一半组件 + 自己的 mock 替换另一半
- **违反 Open-Closed**：加新外设必须改 Board 类
- **测试不灵活**：想单独测 GPIO 行为时，被迫构造整个 Board
- **编译依赖爆炸**：改任何外设头文件，Board 的编译单元全部重编
- **不适合多芯片**：每加一种芯片（STM32F4, GD32 等）就要一个新的 Monolithic 类

### 适用场景

只模拟 STM32F103 一种芯片，不需要灵活组合。

---

## 方案 B：Builder Pattern（建造者模式）

### 设计

```cpp
// include/chips/board_builder.hpp

namespace micro_forge::chips {

class BoardBuilder {
public:
    // 必需组件
    BoardBuilder& cpu(WeakPtr<Bus> bus);  // 或直接接受 Bus&
    BoardBuilder& memory(addr_t base, size_t size, WeakPtr<FlatMemory> mem);

    // 外设（可链式调用）
    BoardBuilder& peripheral(addr_t base, size_t size,
                             WeakPtr<Device> dev, ClockDomain domain);

    // 中断连线
    BoardBuilder& irq(WeakPtr<Device> src, uint8_t irq_n, NvicPeripheral& nvic);

    // 构建
    struct Board {
        Bus bus;
        unique_ptr<CortexM3CPU> cpu;
        SimulationCoordinator coord;
        // 注册表：name → Device*
        unordered_map<string, Device*> peripherals;
    };

    Expected<Board> build();

private:
    // 收集所有注册信息，build() 时统一验证和连线
};

} // namespace micro_forge::chips
```

### 使用方式

```cpp
FlatMemory flash{128 * 1024};
FlatMemory sram{20 * 1024};
Bus bus;
RccPeripheral rcc;
GpioPeripheral gpioa{'A'};
UsartPeripheral usart1;
NvicPeripheral nvic;
SysTickPeripheral systick{nvic};

auto board = BoardBuilder()
    .memory(0x0800'0000, 128_kb, flash.GetWeak())
    .memory(0x2000'0000, 20_kb, sram.GetWeak())
    .peripheral(0x4002'1000, 0x400, rcc.GetWeak(), ClockDomain::Apb2)
    .peripheral(0x4001'0800, 0x400, gpioa.GetWeak(), ClockDomain::Apb2)
    .peripheral(0x4001'3800, 0x400, usart1.GetWeak(), ClockDomain::Apb2)
    .irq(systick.GetWeak(), 15, nvic)
    .build();
```

### 优点

- **高度可组合**：可以自由选择需要的外设
- **测试友好**：可以只注册需要的外设
- **扩展性好**：新外设不需要改 Builder 接口
- **验证点集中**：build() 可以检查地址冲突、必需组件缺失等

### 缺点

- **运行时错误**：漏注册必需组件在 build() 时才报错，不是编译期
- **生命周期负担外移**：外设对象的生命周期由调用者管理，Builder 不持有
- **类型擦退**：通过 `Device*` 访问，丢失具体类型信息（GPIO/USART 等）
- **样板代码仍然存在**：只是从 configure 函数移到了 Builder 调用链
- **Builder 本身的状态管理复杂**：容易出错

### 适用场景

需要灵活组合不同外设配置，或多芯片支持。

---

## 方案 C：分层设计 — Chip Descriptor + System（数据驱动）

### 设计

将「芯片有哪些东西」和「如何运行」分离：

```cpp
// include/chips/descriptor.hpp — 声明式芯片描述

namespace micro_forge::chips {

struct PeripheralSlot {
    addr_t base;
    size_t size;
    string_view type;    // "gpio", "usart", "rcc", "timer" 等
    string_view name;    // "GPIOA", "USART1" 等
    ClockDomain domain;
};

struct MemorySlot {
    addr_t base;
    size_t size;
    string_view name;    // "FLASH", "SRAM"
};

struct ChipDescriptor {
    string_view name;
    span<const MemorySlot> memory;
    span<const PeripheralSlot> peripherals;
    uint32_t default_vector_table;  // 0x00000000 for STM32
    // 中断映射表：peripheral_name → {irq_number}
};

} // namespace micro_forge::chips
```

```cpp
// src/chips/stm32f1/descriptor.cpp — STM32F103 的具体描述

namespace micro_forge::chips::stm32f1 {

static constexpr MemorySlot stm32f103_memory[] = {
    {0x0000'0000, 128_kb, "FLASH_ALIAS"},
    {0x0800'0000, 128_kb, "FLASH"},
    {0x2000'0000, 20_kb,  "SRAM"},
};

static constexpr PeripheralSlot stm32f103_peripherals[] = {
    {0x4002'1000, 0x400, "rcc",    "RCC",    ClockDomain::Apb2},
    {0x4001'0800, 0x400, "gpio",   "GPIOA",  ClockDomain::Apb2},
    {0x4001'0C00, 0x400, "gpio",   "GPIOB",  ClockDomain::Apb2},
    {0x4001'1000, 0x400, "gpio",   "GPIOC",  ClockDomain::Apb2},
    {0x4001'3800, 0x400, "usart",  "USART1", ClockDomain::Apb2},
    {0x4000'0000, 0x400, "timer",  "TIM2",   ClockDomain::Apb1},
    {0xE000'E010, 0x010, "systick","SysTick",ClockDomain::Sysclk},
    {0xE000'E100, 0xC00, "nvic",   "NVIC",   ClockDomain::Sysclk},
};

constexpr ChipDescriptor stm32f103 = {
    .name = "STM32F103",
    .memory = stm32f103_memory,
    .peripherals = stm32f103_peripherals,
    .default_vector_table = 0x0000'0000,
};

} // namespace
```

```cpp
// include/chips/system.hpp — 通用运行时

namespace micro_forge::chips {

class System {
public:
    static Expected<System> instantiate(const ChipDescriptor& desc);

    // 类型化访问
    template <typename T>
    T* find(string_view name);  // nullptr if not found or wrong type

    CortexM3CPU& cpu();
    Bus& bus();
    SimulationCoordinator& coord();

    Expected<void> reset();
    Expected<void> load_elf(span<const uint8_t> data);
    Expected<void> load_bin(uint32_t base, span<const uint8_t> data);

private:
    Bus bus_;
    unique_ptr<CortexM3CPU> cpu_;
    SimulationCoordinator coord_;
    // 类型擦除的存储
    unordered_map<string_view, unique_ptr<Device>> devices_;
    // 内存块
    vector<unique_ptr<FlatMemory>> memories_;
};

} // namespace micro_forge::chips
```

### 使用方式

```cpp
auto sys = System::instantiate(stm32f1::stm32f103);
ASSERT_TRUE(sys.has_value());

auto& s = sys.value();
s.load_elf(firmware);
s.reset();

auto* gpioa = s.find<GpioPeripheral>("GPIOA");
ASSERT_NE(gpioa, nullptr);

s.coord().run(100'000);
```

### 优点

- **最高可扩展性**：新芯片只需写一个 descriptor，不改 System 代码
- **数据与逻辑分离**：芯片描述是纯数据，可以静态常量化
- **统一的运行时**：所有芯片共享 System 类
- **最小使用成本**：3-4 行代码

### 缺点

- **类型擦除**：`find<T>()` 是运行时 dynamic_cast，有性能和设计代价
- **过度抽象**：如果只模拟 STM32F103，这个架构 ROI 很低
- **Device 构造困难**：`instantiate()` 需要一个工厂来根据 type string 创建具体类
- **外设间依赖处理复杂**：SysTick 需要 NVIC 引用，Timer 需要中断线——纯数据描述不够表达
- **编译期安全丧失**：写错名字字符串，编译不过报不出来

### 适用场景

需要支持多种芯片（F1/F4/GD32 等），或芯片描述需要运行时加载。

---

## 方案 D+：跨 ISA 分层 — Machine Runtime + Architecture Adapter + SoC Package（推荐）

### 设计思路

方案 D 的方向是对的，但如果最终要支持 AVR / RISC-V / 8051 / Xtensa，
就不能把顶层抽象命名和职责都绑定到 `Stm32f103Board`。

推荐把系统拆成四层：

1. **Machine/System Runtime**：芯片无关的运行骨架，持有 Bus、CPU slot、Clock、Coordinator、Device registry。
2. **Architecture Adapter**：CPU 架构相关语义，例如 reset、异常/中断入口、trap/return 规则。
3. **SoC Package**：具体 MCU/SoC 的内存映射、外设实例、时钟域、中断路由。
4. **Board Package**：真实开发板或产品板的外部连接，例如 LED、按钮、UART stdout、传感器、外部晶振。

```
Machine/System
  ├── memory::Bus / AddressSpace
  ├── unique_ptr<cpu::CPU>
  ├── sim::VirtualClock
  ├── sim::SimulationCoordinator
  └── optional Device registry / probes

Architecture package
  ├── Cortex-M reset + exception model
  ├── AVR interrupt model
  ├── RISC-V trap/interrupt model
  ├── 8051 interrupt model
  └── Xtensa interrupt model

SoC package
  ├── STM32F103: Flash/SRAM/RCC/GPIO/USART/TIM/NVIC/SysTick
  ├── ATmega328P: Flash/SRAM/EEPROM/SFR/Timer/UART/GPIO
  ├── GD32VF103: RISC-V core + CLINT/PLIC-like interrupt blocks
  └── ESP-class SoC: Xtensa core + interrupt matrix + peripherals

Board package
  ├── BluePill
  ├── Arduino Uno
  └── ESP32 DevKit
```

### 通用运行时骨架

```cpp
// include/chips/machine.hpp

namespace micro_forge::chips {

struct Machine {
    memory::Bus bus;
    std::unique_ptr<cpu::CPU> cpu;
    sim::SimulationCoordinator coord;

    Expected<void> reset();
    Expected<void> load_bin(addr_t base, std::span<const uint8_t> data);
    Expected<void> load_elf(std::span<const uint8_t> data);
    void run(size_t max_steps);

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;
};

} // namespace micro_forge::chips
```

`Machine` 不知道 STM32、不知道 NVIC，也不应该知道 Cortex-M。
它只负责运行、调度、总线访问和生命周期边界。

### 架构中断接口

当前 `CortexM3CPU` 直接持有 `NvicPeripheral*`。这对 Phase 3/4 足够，
但会阻塞跨 ISA：AVR 没有 NVIC，RISC-V 可能使用 CLINT/PLIC，8051 使用 SFR
里的 IE/IP 体系，Xtensa 也有自己的中断矩阵。

建议引入一个最小的架构中断控制接口：

```cpp
namespace micro_forge::cpu {

struct InterruptRequest {
    uint32_t line;
    uint8_t priority;
};

struct InterruptController {
    virtual ~InterruptController() = default;
    virtual std::optional<InterruptRequest> poll() = 0;
    virtual void acknowledge(uint32_t line) = 0;
    virtual void complete(uint32_t line) = 0;
};

} // namespace micro_forge::cpu
```

之后：

- `NvicPeripheral` 可以同时是 MMIO `Device` 和 Cortex-M `InterruptController`
- AVR interrupt controller 可以实现自己的 `InterruptController`
- RISC-V 的 PLIC/CLINT 可以提供适配层
- CPU 只依赖架构接口，不依赖具体外设类

### STM32F103 作为第一个 SoC Package

```cpp
// include/chips/stm32f1/stm32f103_soc.hpp

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

    periph::GpioPeripheral& gpio(char id);
};

class Stm32f103Soc {
public:
    static Expected<std::unique_ptr<Stm32f103Soc>> create();

    chips::Machine& machine() { return machine_; }
    Stm32f103Parts& parts() { return parts_; }

    Expected<void> reset();
    Expected<void> load_bin(addr_t base, std::span<const uint8_t> data);
    Expected<void> load_elf(std::span<const uint8_t> data);
    void run(size_t max_steps);

    Stm32f103Soc(const Stm32f103Soc&) = delete;
    Stm32f103Soc& operator=(const Stm32f103Soc&) = delete;
    Stm32f103Soc(Stm32f103Soc&&) = delete;
    Stm32f103Soc& operator=(Stm32f103Soc&&) = delete;

private:
    Stm32f103Soc();

    chips::Machine machine_;
    Stm32f103Parts parts_;
};

} // namespace micro_forge::chips::stm32f1
```

### 组装原则

`Stm32f103Soc::create()` 负责：

1. 创建地址稳定的 `Stm32f103Soc`
2. 映射 Flash/SRAM/boot alias
3. 映射 RCC/GPIO/USART/TIM/SysTick/NVIC
4. 创建 Cortex-M3 CPU，并接入中断控制器适配层
5. 配置 `SimulationCoordinator`
6. 注册 tickable 外设及 clock domain

```cpp
Expected<std::unique_ptr<Stm32f103Soc>> Stm32f103Soc::create() {
    auto soc = std::unique_ptr<Stm32f103Soc>(new Stm32f103Soc());

    auto& bus = soc->machine_.bus;
    auto& c = soc->parts_;

    TRY(bus.map(memory::region(0x0000'0000, 128_kb, c.flash.GetWeak())));
    TRY(bus.map(memory::region(0x0800'0000, 128_kb, c.flash.GetWeak())));
    TRY(bus.map(memory::region(0x2000'0000, 20_kb, c.sram.GetWeak())));

    TRY(bus.map(memory::region(0x4002'1000, 0x400, c.rcc.GetWeak())));
    TRY(bus.map(memory::region(0x4001'0800, 0x400, c.gpioa.GetWeak())));
    TRY(bus.map(memory::region(0x4001'0C00, 0x400, c.gpiob.GetWeak())));
    TRY(bus.map(memory::region(0x4001'1000, 0x400, c.gpioc.GetWeak())));
    TRY(bus.map(memory::region(0x4001'3800, 0x400, c.usart1.GetWeak())));
    TRY(bus.map(memory::region(0x4000'0000, 0x400, c.tim2.GetWeak())));
    TRY(bus.map(memory::region(0xE000'E010, 0x010, c.systick.GetWeak())));
    TRY(bus.map(memory::region(0xE000'E100, 0xC00, c.nvic.GetWeak())));

    soc->machine_.cpu = std::make_unique<CortexM3CPU>(bus.GetWeak());
    // 过渡期可以继续 set_nvic(c.nvic)，后续替换为 InterruptController。

    soc->machine_.coord.set_cpu(soc->machine_.cpu->GetWeak());
    soc->machine_.coord.add_tickable(c.systick.GetWeak(),
                                     domain_index(ClockDomain::Sysclk));
    soc->machine_.coord.add_tickable(c.tim2.GetWeak(),
                                     domain_index(ClockDomain::Apb1));

    return soc;
}
```

### 使用方式

**最简用例（加载运行固件）**：

```cpp
auto soc = stm32f1::Stm32f103Soc::create();
ASSERT_TRUE(soc.has_value());

(*soc)->load_elf(firmware);
(*soc)->reset();
(*soc)->run(100'000);
```

**测试用例（访问具体外设）**：

```cpp
auto soc = stm32f1::Stm32f103Soc::create();
ASSERT_TRUE(soc.has_value());

auto& parts = (*soc)->parts();
parts.nvic.set_pending(15);
parts.gpioa.write(/* ... */);
```

**板级扩展（后续）**：

```cpp
auto board = boards::BluePill::create();
board->soc().load_elf(firmware);
board->connect_uart1_stdout();
board->run(100'000);
```

### 优点

- **跨 ISA 方向正确**：通用层不依赖 STM32、Cortex-M 或 NVIC
- **Phase 4 可落地**：仍能快速解决当前组装样板代码
- **类型安全**：SoC package 内部保留强类型 `parts()`
- **地址稳定**：通过 `unique_ptr` 创建不可移动 SoC
- **测试友好**：外设仍可直接访问，不必通过字符串查找
- **未来可扩展**：AVR/RISC-V/8051/Xtensa 可以各自提供 Architecture Adapter + SoC Package
- **避免过早数据驱动**：descriptor 只作为 metadata，不承担外设行为和 wiring

### 缺点

- **层次比单体 Board 多**：需要理解 Machine / Architecture / SoC / Board
- **短期代码量略多**：会多出一些骨架类型
- **需要逐步清理现有耦合**：尤其是 `CortexM3CPU -> NvicPeripheral*`
- **不是纯插件化**：新增 MCU 仍需要写 C++ package，而不是只写 JSON/表格

### 适用场景

当前项目最佳平衡点：Phase 4 可以快速落地 STM32F103，同时不会把未来 AVR /
RISC-V / 8051 / Xtensa 支持堵死。

---

## 方案对比总结

| 维度 | A: Monolithic | B: Builder | C: Data-Driven | D+: Cross-ISA Layered (推荐) |
|------|:---:|:---:|:---:|:---:|
| 使用简单度 | ★★★★★ | ★★★ | ★★★★ | ★★★★★ |
| 测试灵活性 | ★★ | ★★★★★ | ★★★★ | ★★★★ |
| 类型安全 | ★★★★★ | ★★ | ★ | ★★★★★ |
| 扩展性（新芯片） | ★ | ★★★ | ★★★★★ | ★★★★ |
| 扩展性（跨 ISA） | ★ | ★★ | ★★★★ | ★★★★★ |
| 编译期检查 | ★★★★★ | ★★ | ★ | ★★★★★ |
| 实现复杂度 | 低 | 中 | 高 | 低-中 |
| 与现有代码兼容 | ★★★★ | ★★★ | ★★ | ★★★★★ |
| WeakPtr 安全 | 需注意移动 | 调用者负责 | 框架管理 | 需注意移动 |

---

## 额外问题：WeakPtr 与移动语义

无论选哪个方案，都有一个必须解决的共性问题：

**外设类含 `WeakPtrFactory<self> weak_factory_{this}`，移动后 `this` 改变，WeakPtr 悬空。**

当前所有测试都是先构造、再连线，没有移动。但 SoC/Board 类如果值返回：

```cpp
auto soc = Stm32f103Soc::create();  // 如果按值返回，会涉及不可移动成员
```

更关键的是，`WeakPtrFactory` 本身 delete 了 copy/move。也就是说问题不只是
NRVO 是否生效，而是这些对象天然不适合作为普通可移动值来承载。

**解决方案**：

1. **返回 `unique_ptr<Stm32f103Soc>`**——最安全，地址绝对稳定
   ```cpp
   static Expected<unique_ptr<Stm32f103Soc>> create();
   ```
   缺点：堆分配，但 SoC/Board 整个生命周期很长，不在乎一次 malloc

2. **工厂函数内直接构造 + `std::move` 只移动 non-WeakPtr 成员**——复杂，不推荐

3. **延迟初始化 WeakPtrFactory**——让 WeakPtrFactory 在首次 GetWeak() 时才绑定 this
   改动较大但一劳永逸

**建议采用方案 1**（返回 unique_ptr），简单直接。

---

## 额外问题：CPU 与中断控制器耦合

现状：

```cpp
class CortexM3CPU {
    periph::NvicPeripheral* nvic_ = nullptr;
};
```

这会把 Cortex-M/NVIC 假设扩散到 CPU 实现里。对于跨 ISA 目标，建议逐步改成：

```cpp
class CortexM3CPU {
    cpu::InterruptController* interrupt_controller_ = nullptr;
};
```

迁移可以分两步：

1. Phase 4 先保留 `set_nvic()`，由 `Stm32f103Soc::create()` 继续调用，避免扩大改动面。
2. 后续新增 `set_interrupt_controller()`，让 `NvicPeripheral` 实现该接口，再移除 raw NVIC 依赖。

这样不会阻塞当前 STM32F103 验收，也能为 AVR/RISC-V/8051/Xtensa 留出接口空间。

---

## 下一步

1. 更新 Phase 4 里程碑文档中的 D4-8：从 `configure_stm32f103()` 改为 `Stm32f103Soc::create()`
2. 先实现最小 `chips::Machine` + `stm32f1::Stm32f103Parts` + `stm32f1::Stm32f103Soc`
3. `Stm32f103Soc::create()` 返回 `Expected<std::unique_ptr<Stm32f103Soc>>`
4. 将现有 `configure_memory()` / `configure_interrupt_devices()` 的逻辑迁移或复用到 SoC package 内
5. Phase 4 暂时保留 `CortexM3CPU::set_nvic()`，但在文档和接口上标记为过渡设计
6. 下一阶段引入 `cpu::InterruptController`，解除 CPU 对具体 NVIC 类型的依赖
7. Board package 暂缓，等 GPIO/USART/外部输入输出需求稳定后再设计 BluePill/Arduino 等真实板级抽象
