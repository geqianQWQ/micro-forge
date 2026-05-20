# 003 - 外设与 SoC

> v0.1.0 开发笔记 | 2026-05-20

## 1. 外设架构

所有外设继承 `periph::Device`（见笔记 002）。芯片专用外设使用 `Stm32f1` 前缀，位于 `include/chips/stm32f1/` 和 `src/chips/stm32f1/`。

通用接口作为基类：
- `periph::Gpio` — 引脚读写、配置、输入模拟
- `periph::Timer` — 使能、预分频、自动重载、计数器
- `periph::SerialPort` — 字节发送、输出回调
- `periph::ClockController` — 时钟使能查询与控制

STM32F1 实现同时继承 Device（MMIO 接口）和通用接口（抽象接口），例如 `Stm32f1Gpio : public Device, public Gpio`。

## 2. NVIC（嵌套向量中断控制器）

### 架构（`include/periph/nvic.hpp`）

**Pull 模式**：NVIC 不持有 CPU 引用。CPU 每步主动轮询 NVIC 的 `has_pending_irq()` 和 `highest_pending_irq()`。无循环引用。

### 寄存器

| 寄存器组 | 数组大小 | 功能 |
|----------|---------|------|
| ISER/ICER | `uint32_t[8]` | 中断使能/禁能（240 个 IRQ） |
| ISPR/ICPR | `uint32_t[8]` | 中断挂起/清除挂起 |
| IP | `uint8_t[240]` | 优先级（每个 IRQ 8 位） |

### 公共查询 API（CPU 热路径）

- `has_pending_irq()` — 扫描 ISPR 寄存器
- `highest_pending_irq()` — 返回最高优先级的挂起 IRQ 号（无则返回 0xFF）
- `irq_priority(irq_n)` — 查询优先级
- `is_enabled(irq_n)` — 检查使能位（带边界检查 `irq_n < kMaxIrq`）

### 外部触发

SysTick 等外设通过 `set_pending(n)` 触发中断。CPU 通过 `clear_pending(n)` 在中断入口后清除。

## 3. SysTick

### 实现（`include/periph/systick.hpp`）

注册到 SimulationCoordinator 的 Sysclk 时钟域，`tick(cycles)` 由协调器驱动。

### 寄存器

| 寄存器 | 偏移 | 功能 |
|--------|------|------|
| CTRL | 0x00 | 位 0: ENABLE, 位 1: TICKINT, 位 16: COUNTFLAG |
| LOAD | 0x04 | 重载值 |
| VAL | 0x08 | 当前值（写任意值清零） |

### tick() 逻辑

```
if (CTRL & ENABLE):
    val_ -= ticks  (while 循环处理多圈溢出)
    if val_ 计数到 0:
        COUNTFLAG = 1
        val_ = LOAD
        if (CTRL & TICKINT):
            irq_cb_()  // 通过回调触发 NVIC set_pending(15)
```

使用 while 循环而非简单减法，处理零 LOAD 值跳过和多次溢出场景。

## 4. SCB（系统控制块）

### 实现（`include/periph/scb.hpp`）

映射于 `0xE000ED00-0xE000ED8F`。

### 寄存器

| 寄存器 | 复位值 | 功能 |
|--------|--------|------|
| CPUID | `0x412FC230` | 只读，Cortex-M3 r2p0 标识 |
| ICSR | 0 | 中断控制与状态 |
| VTOR | 0 | 向量表偏移（可读写） |
| AIRCR | `0xFA050000` | 应用中断与复位控制（VECTKEY 写保护） |
| SCR | 0 | 系统控制 |
| CCR | `0x00000200` | 配置与控制（STKALIGN 位置位） |
| SHP[12] | 全 0 | 系统处理器优先级 |
| SHCSR | 0 | 系统处理器控制与状态 |

### VTOR 联动

SCB VTOR 写入触发回调 `vtor_cb_()`，该回调在 SoC 组装时连接到 `cortex_m3_cpu->set_vector_table_base()`，保证 CPU 的向量表基址与 SCB VTOR 同步。

## 5. STM32F1 芯片专用外设

### RCC（`Stm32f1Rcc`）

继承 Device + ClockController。

| 寄存器 | 复位值 | 关键行为 |
|--------|--------|---------|
| CR | `0x00000083` | HSI ON；HSERDY/PLLRDY 在对应位使能后立即就绪 |
| CFGR | 0 | SW/SWS 反映当前时钟源切换 |
| APB1ENR/APB2ENR | 0 | 外设时钟使能，查询外设地址判断使能状态 |
| AHBENR | 0 | AHB 总线时钟使能 |

`ClockController` 接口：`is_clock_enabled(peripheral_addr)` 查询 APBxENR 对应位。

**HAL 兼容**：就绪标志（HSERDY, PLLRDY）在使能后立即设置，避免 HAL 轮询等待。

### GPIO（`Stm32f1Gpio`）

继承 Device + Gpio。构造参数 `port_id`（'A'/'B'/'C'），决定 `name()` 返回值。

| 寄存器 | 偏移 | 复位值 | 功能 |
|--------|------|--------|------|
| CRL | 0x00 | `0x44444444` | 端口低 8 位配置 |
| CRH | 0x04 | `0x44444444` | 端口高 8 位配置 |
| IDR | 0x08 | 0 | 输入数据（只读） |
| ODR | 0x0C | 0 | 输出数据 |
| BSRR | 0x10 | — | 位设置/重置（低 16 位设置，高 16 位重置） |
| BRR | 0x14 | — | 位重置（低 16 位） |
| LCKR | 0x18 | 0 | 配置锁定 |

ODR 变更触发日志：`[GPIO] GPIOx.PINn -> HIGH/LOW`。BSRR 写入自动更新 ODR 并触发对应 pin 的 change callback。

### USART（`Stm32f1Usart`）

继承 Device + SerialPort。

| 寄存器 | 偏移 | 复位值 | 功能 |
|--------|------|--------|------|
| SR | 0x00 | `0x000000C0` | TXE=1, TC=1（始终就绪） |
| DR | 0x04 | 0 | 数据寄存器 |
| BRR | 0x08 | 0 | 波特率 |
| CR1 | 0x0C | 0 | 控制寄存器 1 |
| CR2 | 0x10 | 0 | 控制寄存器 2 |
| CR3 | 0x14 | 0 | 控制寄存器 3 |

**轮询友好**：SR 的 TXE 和 TC 始终为 1，DR 写入触发 `output_()` 回调输出字节。固件中 `HAL_UART_Transmit` 的轮询模式不会阻塞。

### Timer（`Stm32f1Timer`）

继承 Device + Timer。

| 寄存器 | 偏移 | 功能 |
|--------|------|------|
| CR1 | 0x00 | 控制寄存器（CEN 使能位） |
| DIER | 0x0C | DMA/中断使能 |
| SR | 0x10 | 状态（UIF 更新中断标志） |
| PSC | 0x28 | 预分频器 |
| ARR | 0x2C | 自动重载值 |
| CNT | 0x24 | 计数器 |

### tick() 逻辑

```
if (CR1 & CEN):
    prescaled = cycles / (psc_ + 1)
    cnt_ += prescaled
    prescaler_residual_ += cycles % (psc_ + 1)  // 保留余数避免漂移
    if prescaler_residual_ >= (psc_ + 1):
        cnt_++
        prescaler_residual_ -= (psc_ + 1)
    if cnt_ >= arr_:
        SR |= UIF
        cnt_ = 0
```

### AFIO（`Stm32f1Afio`）

| 寄存器 | 功能 |
|--------|------|
| EVCR | 事件控制 |
| MAPR | 复用功能映射 |
| EXTICR[4] | 外部中断配置 |
| MAPR2 | 复用功能映射 2 |

保留地址读取返回 0，写入忽略。HAL 初始化会写 AFIO 但不需要模拟功能。

### FLASH 接口（`Stm32f1Flash`）

| 寄存器 | 复位值 | 功能 |
|--------|--------|------|
| ACR | `0x00000030` | PRFTBE=1, LATENCY=1（支持 HAL_RCC_ClockConfig） |
| KEYR | 0 | 解锁密钥 |
| OPTKEYR | 0 | 选项字节密钥 |
| SR | 0 | 状态（永不忙） |
| CR | `0x00000080` | LOCK=1（复位后锁定） |

SR 永不忙——HAL 擦除/编程轮询不会卡死。

## 6. SoC 抽象

### 三层结构

#### Machine（`include/chips/machine.hpp`）

芯片无关的运行时容器：
- `unique_ptr<SimulationCoordinator> coord`
- `unique_ptr<CPU> cpu`
- `unique_ptr<Bus> bus`
- 方法：`load_bin()`, `load_elf()`, `run()`

未来新芯片复用 Machine，只需更换 CPU 和 Parts。

#### Stm32f103Parts（`include/chips/stm32f1/stm32f103_soc.hpp`）

透明结构体，持有所有 STM32F103 设备实例：
- Flash（128KB）+ SRAM（20KB）FlatMemory
- NVIC, SysTick, SCB
- RCC, AFIO, FLASH 接口
- GPIOA, GPIOB, GPIOC
- USART1, TIM2

成员顺序重要（C++ 逆序销毁）：NVIC 必须在 SysTick 之前声明（SysTick 持有 NVIC 引用）。

便捷方法：`gpio('A')`, `serial()`, `timer()`, `clocks()`

#### Stm32f103Soc

组装层，`create()` 工厂执行：
1. 分配 Parts + Bus + Machine
2. 映射内存区域（Flash, SRAM, Boot 别名）
3. 映射所有外设到 Bus
4. 创建 CortexM3CPU，注入 Bus WeakPtr
5. 连接 NVIC 到 CPU（`set_nvic()`）
6. 连接 SCB VTOR 回调到 CPU（`set_vector_table_base()`）
7. 配置 SimulationCoordinator（CPU, SysTick→Sysclk, TIM2→APB1）

不可移动/不可复制（WeakPtrFactory 约束）。

### 设计决策（D+ 跨 ISA 分层）

当前实现是 D+ 方案的 STM32F103 实例化。未来新芯片获得自己的 Parts/Soc，无需修改 Machine 或 CPU。

## 7. 固件加载与复位序列

### BinaryLoader（`include/loader/binary_loader.hpp`）

`load_binary(base_addr, data)` → 将原始二进制数据写入 Bus。处理非对齐尾部块（1-3 字节使用对应 Width 写入）。

### ElfLoader（`include/loader/elf_loader.hpp`）

`load_elf(Bus&, data)` → 解析 ELF32 头部，遍历 PT_LOAD 段，写入 Bus。返回 `entry_point`。

- 处理 BSS 段零填充（memsz > filesz 的部分）
- 处理非 4 对齐的尾部负载
- 返回 `ElfLoadResult { entry_point }`

### 复位序列（`src/arch/arm/cortex_m3/cortex_m3_reset.cpp`）

1. 从 `vector_table_base_ + 0` 读取初始 SP
2. 从 `vector_table_base_ + 4` 读取初始 PC
3. PC 位 0 必须为 1（Thumb 模式指示）
4. LR = `0xFFFFFFFF`
5. 日志：`[RESET] SP=... PC=...`

## 8. STM32CubeF1 HAL 子模块

### 集成方式

Git submodule 位于 `third_party/STM32CubeF1`（v1.8.6）。仅当子模块存在时启用 HAL 固件编译，CMake 会提示缺失。

### HAL UART 轮询固件

完整初始化链：`HAL_Init → SystemClock_Config → MX_GPIO_Init → MX_USART1_UART_Init → HAL_UART_Transmit`

所需 HAL 源文件：`stm32f1xx_hal.c`, `stm32f1xx_hal_rcc.c`, `stm32f1xx_hal_gpio.c`, `stm32f1xx_hal_uart.c`, `stm32f1xx_hal_cortex.c`

编译宏：`STM32F103xB`, `USE_HAL_DRIVER`, `HSE_VALUE=8000000`

交叉编译使用 `arm-none-eabi-gcc`，每个示例有独立的 `firmware/` 目录和链接脚本。
