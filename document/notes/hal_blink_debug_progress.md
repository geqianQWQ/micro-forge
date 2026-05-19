# hal_blink 调试进度 (2026-05-19)

## 当前状态

已修复，`hal_blink_runner` 可以成功运行：

```text
[RESET] SP=0x20005000 PC=0x08000040
GPIO PA5 toggled 2006 times
```

运行命令：

```sh
cmake --build build --target test_cortex_m3 hal_blink_runner
./build/test/test_cortex_m3
cd build/examples/hal_blink && ./hal_blink_runner hal_blink.elf
```

## 根因

### 1. CBNZ/CBZ 被误解码为 POP

故障点最初出现在：

```asm
0800013c: b908       cbnz r0, 8000142
```

`0xB908` 属于 Thumb-16 `CBNZ` 编码，但原 16-bit decoder 没有先识别
`CBZ/CBNZ`，导致它落入 `10111` 这组 `POP/Hints` 解码路径。结果 CPU
从当前栈帧中错误弹出 PC，PC 被写成 `0x00000000` 或 boot alias 地址，随后
SysTick 不断打断错误路径，最终栈耗尽或 fault。

修复：

- 在 `execute_16bit()` 的主 `switch` 前增加 `CBZ/CBNZ` 精确匹配。
- 新增单测 `CortexM3Test.CbzAndCbnzBranchWithoutTouchingStack`，确保
  `CBZ/CBNZ` 分支不会触碰 SP。

### 2. HAL 初始化继续推进后缺少 Thumb-2 寄存器移位

修复 `CBNZ` 后，程序推进到 `HAL_NVIC_SetPriority()`，暴露下一条缺失指令：

```asm
080001f2: fa03 f30c  lsl.w r3, r3, ip
```

同一段 HAL 代码还依赖 `lsrs.w` 的 Z flag 来做分支判断。原 decoder 只覆盖了
部分 `EAxx` 数据处理形式，没有覆盖 `FAxx` 的寄存器移位形式。

修复：

- 增加 Thumb-2 `LSL/LSR/ASR/ROR register` 解码。
- 当指令带 S bit 时更新 N/Z 标志，满足 HAL 中 `LSRS.W` 后续分支。

## 清理

已清理 `cortex_m3.cpp` 中这次排查用的临时诊断输出：

- `write_reg()` 的 `[TRACE-TRANS]`
- `step()` 的 PC 范围 trace
- `check_and_handle_interrupt()` 的 SysTick `[DIAG]`
- `interrupt_entry_system()` 的 handler/vector/stack `[DIAG]`

GPIO 外设自身仍会输出 `[GPIO] GPIOA.PIN5 -> HIGH/LOW`，这是现有 GPIO
变更日志行为，不属于本次 CPU 临时诊断。

## 验证结果

- `cmake --build build --target test_cortex_m3 hal_blink_runner`：通过
- `./build/test/test_cortex_m3`：17/17 通过
- `./build/examples/hal_blink/hal_blink_runner hal_blink.elf`：返回码 0，
  `GPIO PA5 toggled 2006 times`
