# Phase 6.3 · Architecture And Peripheral Mappings

## CPU 状态

| 状态 | 当前情况 | Phase 6 要求 |
|------|----------|--------------|
| `PRIMASK` | 已有部分支持 | 与 `CPSIE/CPSID`、`MRS/MSR` 行为统一 |
| `BASEPRI` | 缺失 | 影响 `check_and_handle_interrupt()`，屏蔽优先级数值 `>= BASEPRI` 的 IRQ |
| `FAULTMASK` | 缺失 | 先读写保存；完整 fault 语义可后续增强 |
| `CONTROL` | 字段存在但不完整 | 支持 MSP/PSP 选择和 `MRS/MSR` |
| `MSP` / `PSP` | 只有 R13 单栈 | 建议引入独立 `msp_` / `psp_` 或等价抽象 |
| `VTOR` | CPU 内部默认 `0x08000000` | 由 SCB `VTOR` MMIO 写入同步 |

## Exception 编号规则

必须统一 exception number 和 external IRQ number：

| 类型 | 编号规则 | Vector table index |
|------|----------|--------------------|
| Reset | exception 1 | 1 |
| HardFault | exception 3 | 3 |
| SVC | exception 11 | 11 |
| PendSV | exception 14 | 14 |
| SysTick | exception 15 | 15 |
| 外部 IRQ n | IRQ number `n` | `16 + n` |

当前中断路径需要重点审查 SysTick 和外部 IRQ 的映射，避免把 SysTick 当成 external IRQ 15 后读取 vector entry 31。

## SCB

新增 `ScbPeripheral`，建议映射：

```text
0xE000ED00-0xE000ED8F
```

| 寄存器 | 地址 | 最小行为 |
|--------|------|----------|
| `CPUID` | `0xE000ED00` | 只读，返回 Cortex-M3 合理值 |
| `ICSR` | `0xE000ED04` | 先支持 pending 状态字段最小读写 |
| `VTOR` | `0xE000ED08` | 可读写，写入同步 CPU vector base |
| `AIRCR` | `0xE000ED0C` | 支持 `VECTKEY` 写保护和 `PRIGROUP` |
| `SCR` | `0xE000ED10` | 可读写保存 |
| `CCR` | `0xE000ED14` | 可读写保存 |
| `SHPR1-3` | `0xE000ED18-0xE000ED20` | 保存系统异常优先级 |
| `SHCSR` | `0xE000ED24` | 可读写保存，fault 完整语义后续增强 |

实现建议：

- `ScbPeripheral` 放在 `include/periph` / `src/periph`。
- CPU 提供窄接口给 SCB：`set_vector_table_base()`、`vector_table_base()`、`set_priority_grouping()`。
- `configure_interrupt_devices()` 里将 SCB 和 SysTick/NVIC 一起映射到 PPB。

## RCC

当前 `Stm32f1Rcc` 已有基本寄存器，但 HAL clock config 会轮询 ready flags。Phase 6 最小策略是“配置写入可读回，ready 位保持 ready 或按写入立即 ready”。

| 寄存器/字段 | 地址 | 最小行为 |
|-------------|------|----------|
| `CR.HSION/HSIRDY` | `0x40021000` | HSI 默认 on/ready |
| `CR.HSEON/HSERDY` | `0x40021000` | 写 HSEON 后 HSERDY 立即置位 |
| `CR.PLLON/PLLRDY` | `0x40021000` | 写 PLLON 后 PLLRDY 立即置位 |
| `CFGR.SW/SWS` | `0x40021004` | 写 SW 后 SWS 反映相同 clock source |
| `APB2ENR` | `0x40021018` | AFIO/GPIOA/USART1 时钟 enable 可读回 |
| `APB1ENR` | `0x4002101C` | 保留 TIM2 等已有行为 |

## FLASH

新增 `Stm32f1Flash`，映射：

```text
0x40022000-0x400223FF
```

首批只需要 `FLASH_ACR` 支持 `HAL_RCC_ClockConfig()` 设置 latency：

| 寄存器 | 偏移 | 最小行为 |
|--------|------|----------|
| `ACR` | `0x00` | 可读写保存 latency、prefetch bit |
| `KEYR/OPTKEYR` | `0x04/0x08` | 可先写入忽略 |
| `SR` | `0x0C` | 返回非 busy |
| `CR` | `0x10` | 可读写保存 |

## AFIO

新增 `Stm32f1Afio`，映射：

```text
0x40010000-0x400103FF
```

| 寄存器 | 偏移 | 最小行为 |
|--------|------|----------|
| `EVCR` | `0x00` | 可读写保存 |
| `MAPR` | `0x04` | 可读写保存，支持 SWJ/JTAG 配置位 |
| `EXTICR1-4` | `0x08-0x14` | 可读写保存 |
| `MAPR2` | `0x18` | 可读写保存 |

## GPIOA

HAL UART fixture 会配置 USART1 默认引脚：

| 引脚 | 模式 |
|------|------|
| PA9 | USART1 TX，Alternate Function Push-Pull |
| PA10 | USART1 RX，Input floating 或 pull-up |

Phase 6 重点：

- `CRH` 对 PA9/PA10 的写入可读回。
- HAL 对 `BSRR/BRR` 的访问不 fault。
- 不强制做 AF routing，USART1 `DR` 写入直接输出即可。

## USART1

| 寄存器/字段 | 最小行为 |
|-------------|----------|
| `SR.TXE` | 始终为 1，允许 `HAL_UART_Transmit()` 继续 |
| `SR.TC` | 始终为 1 或写 `DR` 后立即置位 |
| `DR` write | 输出低 8 位到 `SerialPort` callback/stdout |
| `BRR` | 可读写保存 |
| `CR1/CR2/CR3` | 可读写保存，至少不因 UE/TE/RE 设置 fault |
| timeout loop | `HAL_GetTick()` 需要随 SysTick 或模拟时间推进 |

## 相关文件

| 工作包 | 主要文件 |
|--------|----------|
| SCB | `include/periph/scb.hpp`、`src/periph/scb.cpp`、`src/chips/stm32f1/interrupt_config.cpp` |
| FLASH | `include/chips/stm32f1/stm32f1_flash.hpp`、`src/chips/stm32f1/stm32f1_flash.cpp` |
| AFIO | `include/chips/stm32f1/stm32f1_afio.hpp`、`src/chips/stm32f1/stm32f1_afio.cpp` |
| RCC/GPIO/USART 增强 | `src/chips/stm32f1/stm32f1_rcc.cpp`、`src/chips/stm32f1/stm32f1_gpio.cpp`、`src/chips/stm32f1/stm32f1_usart.cpp` |
| SoC 组装 | `include/chips/stm32f1/stm32f103_soc.hpp`、`src/chips/stm32f1/stm32f103_soc.cpp`、`src/chips/stm32f1/peripheral_config.cpp` |
