# Phase 6.4 · Tests And Diagnostics

## 单元测试

| 测试 | 覆盖 |
|------|------|
| `test_cortex_m3` | 新增 Thumb/Thumb-2 指令真实编码 |
| `test_interrupt` | SVC、SysTick、external IRQ vector index、`VTOR` 重定位 |
| `test_nvic` | `BASEPRI` 屏蔽、优先级读写 |
| `test_stm32f1_periph` | RCC ready flags、FLASH ACR、AFIO、USART polling 状态位 |
| `test_soc` | 新外设映射不冲突，PPB/外设地址可访问 |

## HAL E2E

新增 `test_e2e` case：

```text
E2E.HalUartTransmit
```

验收：

- HAL fixture ELF 存在。
- `Stm32f103Soc::load_elf()` 成功。
- 运行固定步数后 CPU 不为 `Faulted`。
- USART output 包含完整字符串 `Hello from STM32 HAL UART`。
- 若 submodule 缺失，CMake 明确提示并跳过 HAL fixture，不把测试伪装成通过。

## 回归

现有测试必须继续通过：

```bash
ctest --test-dir build --output-on-failure
```

重点关注：

- bare-metal hello world。
- GPIO blink。
- SysTick roundtrip。
- ELF loader。
- memory bus overlap 和 unmapped 行为。

## 诊断策略

HAL 接入早期建议默认保留以下诊断能力：

- CPU fault 时打印 PC、halfword、可选第二 halfword。
- MMIO trace 能筛出最后一次 unmapped/read-only/unaligned 访问。
- HAL E2E 失败时输出 PC、LR、SP、handler mode、最近 MMIO。

推荐排查顺序：

1. `IllegalInstructions`：回到指令 mappings，先补 CPU。
2. `Unmapped`：查寄存器 mappings，补 MMIO 区域。
3. HAL timeout：查 ready flags、`HAL_GetTick()`、SysTick、RCC/FLASH 状态位。
4. 输出为空：查 USART `SR.TXE/TC`、`DR` write、fixture 是否真的调用 `HAL_UART_Transmit()`。

## 验收清单

- [ ] P6.0 第一批指令单测通过。
- [ ] P6.0 第二批指令单测通过。
- [ ] `.gitmodules` 包含 `third_party/STM32CubeF1`。
- [ ] submodule 使用 recursive init/update。
- [ ] HAL UART fixture 可编译。
- [ ] SCB `VTOR` 写入后 vector table 重定位有效。
- [ ] RCC/FLASH 不导致 `SystemClock_Config()` timeout。
- [ ] AFIO/GPIO/USART1 支持 HAL UART init。
- [ ] `HAL_UART_Transmit()` 输出完整字符串。
- [ ] Phase 5 E2E 全部继续通过。
