# Phase 6.0 · Instruction First

## 目标

先增强 CPU 指令覆盖，再接 HAL fixture。HAL/CMSIS 编译产物会大量触发当前未实现的 Thumb/Thumb-2 和 Cortex-M 系统指令；如果先搭 HAL 外设，调试会被 `IllegalInstructions` 淹没。

## 第一批：启动必需

| 指令/能力 | 预期行为 | 主要实现位置 | 测试建议 |
|-----------|----------|--------------|----------|
| `DMB` / `DSB` / `ISB` | 单线程模拟器中按 NOP 处理，但必须合法解码 | `src/arch/arm/cortex_m3/cortex_m3.cpp` | 执行后 PC 前进、寄存器不变 |
| `CPSIE i` | 清 `PRIMASK.I` | `CortexM3CPU` | 屏蔽后 pending IRQ 不进入，开启后进入 |
| `CPSID i` | 置 `PRIMASK.I` | `CortexM3CPU` | 同上 |
| `CBZ` / `CBNZ` | 根据低寄存器是否为 0 做 PC-relative branch | 16-bit Thumb decode | taken/not-taken 各一例 |
| `SVC` | 触发 exception 11，后续可先最小 entry | 16-bit Thumb decode + interrupt entry | vector 11 handler 被执行 |
| `MRS/MSR` 扩展 | 支持 `PRIMASK`、`BASEPRI`、`FAULTMASK`、`CONTROL`、`MSP`、`PSP`、`APSR` | 32-bit system instruction decode | 每个 special reg 读写单测 |

## 第二批：HAL 高频数据处理

| 指令/能力 | 用途 | 主要实现位置 | 测试建议 |
|-----------|------|--------------|----------|
| `SXTB` / `SXTH` | signed char/short 参数扩展 | Thumb-2 decode | 正负值扩展 |
| `UXTB` / `UXTH` | unsigned char/short 参数扩展 | Thumb-2 decode | 高位清零 |
| `ORR.W` / `AND.W` / `EOR.W` / `BIC.W` | HAL 寄存器位操作 | Thumb-2 shifted register / immediate | flags 和不更新 flags 形态分开测 |
| `ADD.W` / `SUB.W` | 地址和计数计算 | Thumb-2 shifted register / immediate | carry/overflow 边界 |
| `LSL.W` / `LSR.W` / `ASR.W` / `ROR.W` | 移位与 bitfield 准备 | Thumb-2 decode | 0、1、31、32 相关边界 |
| `SDIV` / `UDIV` | baudrate、timeout、clock 计算 | Thumb-2 decode | 除零策略需固定为 fault 或兼容行为 |

## 第三批：按 HAL 编译产物补齐

| 指令/能力 | 触发场景 | 处理策略 |
|-----------|----------|----------|
| `REV` / `REV16` / `REVSH` | 字节序和半字处理 | 中优先级补齐 |
| `BFI` / `BFC` / `SBFX` / `UBFX` | 位域操作 | HAL 确认触发后补 |
| `TBB` / `TBH` | switch 跳转表 | 跑到真实 case 后补 |
| 通用 `LDM` / `STM` | prologue/epilogue 或结构复制 | 先支持常见寄存器列表 |
| `MLA` / `MLS` / `SMULL` / `UMULL` | 乘法累加 | 低优先级，按 fixture 需要补 |

## 验收

- 新增指令都有真实编码单测。
- `ctest` 中 CPU 指令测试稳定通过。
- HAL fixture 初次接入时，主要失败点应转移到 MMIO/外设，而不是高频指令缺失。

## 相关文件

| 工作 | 文件 |
|------|------|
| 指令实现 | `src/arch/arm/cortex_m3/cortex_m3.cpp` |
| 字段 helper | `include/arch/arm/cortex_m3/thumb_fields.hpp`、`include/arch/arm/cortex_m3/thumb32_fields.hpp` |
| CPU 状态 | `include/arch/arm/cortex_m3/cortex_m3.hpp` |
| 指令测试 | `test/test_cortex_m3.cpp` |
