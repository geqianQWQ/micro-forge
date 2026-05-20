# Phase 6.2 · HAL UART Fixture

## 目标

新增真实 HAL UART polling 验收固件，让模拟器跑通：

```text
Reset_Handler
  -> SystemInit()
  -> main()
     -> HAL_Init()
     -> SystemClock_Config()
     -> MX_GPIO_Init()
     -> MX_USART1_UART_Init()
     -> HAL_UART_Transmit()
```

## 目录

新增：

```text
examples/hal_uart/
examples/hal_uart/firmware/
```

## HAL 源文件

首批只编译 HAL UART 必需源文件：

| 类型 | 路径 |
|------|------|
| CMSIS Core | `third_party/STM32CubeF1/Drivers/CMSIS/Include` |
| CMSIS Device | `third_party/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx` |
| HAL include | `third_party/STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Inc` |
| HAL core | `stm32f1xx_hal.c` |
| HAL RCC | `stm32f1xx_hal_rcc.c` |
| HAL GPIO | `stm32f1xx_hal_gpio.c` |
| HAL UART | `stm32f1xx_hal_uart.c` |
| HAL Cortex | `stm32f1xx_hal_cortex.c` |

推荐编译宏：

```text
STM32F103xB
USE_HAL_DRIVER
HSE_VALUE=8000000
```

## Fixture 行为

- `SystemClock_Config()` 使用 STM32F103 常见 HSE/PLL 配置。
- `MX_GPIO_Init()` 配置 PA9/PA10。
- `MX_USART1_UART_Init()` 初始化 USART1。
- `HAL_UART_Transmit()` 输出固定字符串。

验收字符串：

```text
Hello from STM32 HAL UART
```

## E2E 测试

新增 `test_e2e` case：

```text
E2E.HalUartTransmit
```

验收：

- HAL fixture ELF 存在。
- `Stm32f103Soc::load_elf()` 成功。
- 运行固定步数后 CPU 不为 `Faulted`。
- USART output 包含完整字符串。
- submodule 缺失时测试不注册，CMake 明确提示原因。
