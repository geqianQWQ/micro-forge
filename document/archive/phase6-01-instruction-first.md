# Phase 6.0 · Instruction First

## 目标

先增强 CPU 指令覆盖，再接 HAL fixture。HAL/CMSIS 编译产物会大量触发当前未实现的 Thumb/Thumb-2 和 Cortex-M 系统指令；如果先搭 HAL 外设，调试会被 `IllegalInstructions` 淹没。

---

## 第一批：启动必需

| 指令/能力 | 状态 | 说明 |
|-----------|------|------|
| `DMB` / `DSB` / `ISB` | ❌ 未实现 | 32-bit barrier 指令（`0xF3BF` 前缀），需按 NOP 处理。当前仅 16-bit hint（NOP/WFI/WFE）已实现 |
| `CPSIE i` / `CPSID i` | ❌ 未实现 | 16-bit Thumb：`0xB66x` / `0xB67x`。需操作 `PRIMASK` bit 0 |
| `CBZ` / `CBNZ` | ✅ 已实现 | `cortex_m3.cpp:361-377`，有单测 `CbzAndCbnzBranchWithoutTouchingStack` |
| `SVC` | ⚠️ stub | 16-bit `0xDFxx` 已解码但直接 break（line 808），需触发 exception 11 入口 |
| `MRS` | ⚠️ 部分 | 仅 `PRIMASK`（line 947-950）。缺少 `BASEPRI`、`FAULTMASK`、`CONTROL`、`MSP`、`PSP`、`APSR`（xpsr_）|
| `MSR` | ⚠️ 部分 | 仅 `PRIMASK`（line 952-956）。缺少 `BASEPRI`、`FAULTMASK`、`CONTROL`、`MSP`、`PSP` |
| `MOVW` / `MOVT` | ✅ 已实现 | `cortex_m3.cpp:933-945`。HAL 加载 32-bit 地址常量的核心路径，**文档遗漏标注** |
| `WFI` / `WFE` / `NOP` / `SEV` / `YIELD` | ✅ 已实现 | 16-bit hint 全部按 NOP 处理（line 764-765） |
| 条件码评估 | ✅ 已实现 | `condition_need_execute` 覆盖全部 16 个条件（line 132-171）。注意：无 IT block 支持 |

## 第二批：HAL 高频数据处理

| 指令/能力 | 状态 | 说明 |
|-----------|------|------|
| `SXTB` / `SXTH` | ❌ 未实现 | 16-bit (`0xB240`/`0xB200`) 和 32-bit 均缺 |
| `UXTB` / `UXTH` | ❌ 未实现 | 16-bit (`0xB2C0`/`0xB280`) 和 32-bit 均缺 |
| `ORR.W` / `AND.W` / `EOR.W` / `BIC.W` | ✅ 已实现 | shifted register（line 1100-1153）+ modified immediate（line 958-1015）两条路径均已覆盖 |
| `ADD.W` / `SUB.W` / `RSB.W` | ✅ 已实现 | 同上两条路径均已覆盖 |
| `LSL.W` / `LSR.W` / `ASR.W` / `ROR.W` | ✅ 已实现 | register 形式（line 1155-1199）+ shifted register 内的 immediate 形式 |
| `SDIV` / `UDIV` | ⚠️ 有 bug | 已实现（line 1089-1098），除零返回 0 ✅。**但 mask `0xFFF0==0xFBB0` 同时匹配 UDIV/SDIV，除法 `a/b` 一律无符号，SDIV 结果会错** |
| `LDRB.W` / `STRB.W` | ✅ 已实现 | offset / post-index / pre-index 三种寻址（line 1017-1087） |
| `STRD` / `LDRD` | ✅ 已实现 | immediate offset（line 1201-1236）。**文档遗漏标注** |

## 第三批：按 HAL 编译产物补齐

| 指令/能力 | 状态 | 说明 |
|-----------|------|------|
| `REV` / `REV16` / `REVSH` | ❌ 未实现 | 16-bit Thumb 编码 |
| `BFI` / `BFC` / `SBFX` / `UBFX` | ❌ 未实现 | 32-bit 位域操作 |
| `TBB` / `TBH` | ✅ 已实现 | line 1292-1315 |
| `STM` / `LDM`（32-bit） | ✅ 已实现 | line 1238-1290，支持 writeback |
| `MLA` / `MLS` / `SMULL` / `UMULL` | ❌ 未实现 | 32-bit 乘法累加 |
| `ADC` / `SBC`（modified immediate） | ⚠️ 简化实现 | line 986-990，carry 固定为 0，SBC 简化为 `rn - imm - 1` |

---

## 已有 16-bit Thumb 覆盖（完整清单）

以下均已实现，作为参考：

| 类别 | 指令 | 位置 |
|------|------|------|
| 移位 | `LSL` / `LSR` / `ASR` (imm) | case 0b00000-00010 |
| 算术 | `ADDS` (reg/imm) / `SUBS` (reg/imm) | case 0b00011, 0b00110, 0b00111 |
| 传送 | `MOVS Rd, imm8` | case 0b00100 |
| 比较 | `CMP Rn, imm8` / `CMP high` | case 0b00101, data processing op=A |
| 数据处理 | `AND`/`EOR`/`LSL`/`LSR`/`ASR`/`ADC`/`SBC`/`ROR`/`TST`/`RSB`/`CMN`/`ORR`/`MUL`/`BIC`/`MVN` | case 0b01000 |
| 特殊 | `ADD high` / `MOV high` / `BX` | case 0b01000 special |
| 加载 | `LDR` (literal/imm/reg) / `LDRB`/`LDRH`/`LDRSB`/`LDRSH` | case 0b01001-0b10001 |
| 存储 | `STR`/`STRB`/`STRH` (imm/reg) | case 0b01010-0b10001 |
| 栈 | `PUSH` / `POP` | case 0b10110, 0b10111 |
| SP 操作 | `ADD SP, SP, #imm` / `SUB SP, SP, #imm` / `ADD Rd, SP/PC, #imm` | case 0b10101, 0b10110 |
| 跳转 | `B<cond>` / `B` (unconditional) / `CBZ` / `CBNZ` | case 0b11010-0b11100, 0xB100 |

---

## 已有 32-bit Thumb-2 覆盖（完整清单）

| 类别 | 指令 | 位置 |
|------|------|------|
| 远跳转 | `BL` / `BLX` | line 874-908 |
| 无条件远跳 | `B.W` T4 | line 909-931 |
| 常量加载 | `MOVW` / `MOVT` | line 933-945 |
| 系统寄存器 | `MRS`/`MSR` (仅 PRIMASK) | line 947-956 |
| 数据处理 imm | `AND`/`BIC`/`ORR`/`EOR`/`ADD`/`ADC`/`SBC`/`SUB`/`RSB`.W | line 958-1015 |
| Load/Store byte | `LDRB.W` / `STRB.W` (offset/post/pre) | line 1017-1087 |
| 除法 | `UDIV` / `SDIV` | line 1089-1098 |
| 数据处理 shifted reg | `AND`/`BIC`/`ORR`/`MVN`/`EOR`/`ADD`/`SUB`/`RSB`.W | line 1100-1153 |
| 移位 register | `LSL`/`LSR`/`ASR`/`ROR`.W (register) | line 1155-1199 |
| 双字传送 | `STRD` / `LDRD` | line 1201-1236 |
| 批量传送 | `STM` / `LDM` | line 1238-1290 |
| 跳转表 | `TBB` / `TBH` | line 1292-1315 |

---

## 待实现清单（按优先级）

### P0 — 启动必需，HAL 立即触发

| # | 指令 | 实现位置建议 | 备注 |
|---|------|-------------|------|
| 1 | `CPSIE i` / `CPSID i` | 16-bit Thumb decode | `PRIMASK` 已有字段，只需解码 `0xB66x`/`0xB67x` |
| 2 | `DMB` / `DSB` / `ISB` | 32-bit decode | 前缀 `0xF3BF`，按 NOP 处理 |
| 3 | `SVC` | 16-bit `0xDFxx` | 当前 stub 需扩展为 exception 11 入口 |
| 4 | `MRS`/`MSR` 扩展 | 32-bit 系统指令 | 补 `CONTROL`(已有字段)、`xpsr_`(APSR)、`MSP`/`PSP`(R13 alias)；`BASEPRI`/`FAULTMASK` 需新增字段 |
| 5 | **修复 SDIV** | line 1089-1098 | mask 需区分 UDIV/SDIV；SDIV 应用 `int32_t` 除法 |

### P1 — HAL 高频数据处理

| # | 指令 | 实现位置建议 | 备注 |
|---|------|-------------|------|
| 6 | `SXTB` / `SXTH` / `UXTB` / `UXTH` | 16-bit + 32-bit | 16-bit 编码优先（`0xB2xx`） |
| 7 | `LDR.W` / `STR.W` (word, 32-bit imm offset) | 32-bit decode | 可能已在某路径覆盖，需验证 `0xF8Dx`/`0xF8Cx` 等编码 |

### P2 — 按 HAL 产物补齐

| # | 指令 | 实现位置建议 | 备注 |
|---|------|-------------|------|
| 8 | `REV` / `REV16` / `REVSH` | 16-bit | 字节序处理 |
| 9 | `BFI` / `BFC` / `SBFX` / `UBFX` | 32-bit | 位域操作 |
| 10 | `MLA` / `MLS` / `SMULL` / `UMULL` | 32-bit | 乘法累加 |
| 11 | `ADC`/`SBC` carry 修复 | line 986-990 | 当前 carry 固定为 0，需读取 `PSR_C` |

### P3 — 可能需要

| # | 指令 | 备注 |
|---|------|------|
| 12 | `IT` block | 条件执行块，GCC 有时会生成。若无 IT 则 Thumb 条件指令受限，可能不会触发 |
| 13 | `LDRH.W` / `STRH.W` (32-bit) | 需验证是否已在某路径覆盖 |

---

## 验收

- [ ] P0 全部完成（含 SDIV 修复）
- [ ] P1 全部完成
- [ ] P2 按 HAL 编译产物验证后补齐
- [ ] 新增指令都有真实编码单测
- [ ] `ctest` 中 CPU 指令测试稳定通过
- [ ] HAL fixture 初次接入时，主要失败点应转移到 MMIO/外设，而不是高频指令缺失

## 相关文件

| 工作 | 文件 |
|------|------|
| 指令实现 | `src/arch/arm/cortex_m3/cortex_m3.cpp` |
| 字段 helper | `include/arch/arm/cortex_m3/thumb_fields.hpp`、`include/arch/arm/cortex_m3/thumb32_fields.hpp` |
| CPU 状态 | `include/arch/arm/cortex_m3/cortex_m3.hpp`（`primask_` line 84, `control_` line 85） |
| 指令测试 | `test/test_cortex_m3.cpp` |
| E2E 测试 | `test/test_e2e.cpp` |
