# Phase 6.0e · Cortex-M3 File Split

## 目标

把膨胀的 `src/arch/arm/cortex_m3/cortex_m3.cpp`（1825 行）和 `test/test_cortex_m3.cpp`（697 行）拆成更小的职责单元，每个文件控制在 300-600 行以内。

首批目标不是重写 CPU，而是机械拆分和建立边界。

---

## 一、拆分 `cortex_m3.cpp`（1825 行 → 4 个文件）

现有文件已有 `// ── Section ──` 注释分隔，拆分边界清晰：

| 新文件 | 来源行范围 | 预估行数 | 职责 |
|--------|-----------|---------|------|
| `cortex_m3.cpp`（保留） | 1-371 | ~370 | Core 接口 + 寄存器访问 + Flag 辅助 + 栈操作 + 取指 + step() 主循环 |
| `cortex_m3_thumb16.cpp` | 372-973 | ~600 | 16-bit Thumb 指令解码（`execute_16bit()`） |
| `cortex_m3_thumb32.cpp` | 974-1634 | ~660 | 32-bit Thumb-2 指令解码（`execute_32bit()`） |
| `cortex_m3_interrupt.cpp` | 1635-1825 | ~190 | 中断/异常处理（entry/return/escalation） |

`cortex_m3_reset.cpp`（已存在，55 行）不改动。

### 实施步骤

1. **创建 `cortex_m3_thumb16.cpp`** — 复制通用 include（前 8 行），移入 `execute_16bit()` 方法
2. **创建 `cortex_m3_thumb32.cpp`** — 移入 `execute_32bit()` 方法
3. **创建 `cortex_m3_interrupt.cpp`** — 移入所有中断处理函数：`write_pc()`, `check_and_handle_interrupt()`, `exception_entry_common()`, `interrupt_entry()`, `interrupt_entry_system()`, `interrupt_return()`, `trigger_hardfault()`, `try_escalate_fault()`
4. **原 `cortex_m3.cpp` 保留** Core 接口 + 辅助函数（~370 行）
5. **CMake 确认** — 确保新文件被编译（若用 GLOB_RECURSE 则自动；显式列表需手动添加）

### 注意事项

- 所有方法都是 `CortexM3CPU` 类的成员，拆分后每个 `.cpp` 仍 `#include "cortex_m3.hpp"` 即可
- 匿名 namespace 中的 `cpu_error_name()` 辅助函数只在原文件使用，不移动
- 头文件 `cortex_m3.hpp` **不需要改动**
- 保持 `CortexM3CPU` 对外 API 不变
- 不顺手改指令行为

---

## 二、拆分 `test_cortex_m3.cpp`（697 行 → 4 个文件 + 1 个公共头）

按测试主题分组：

| 新文件 | 测试范围 | 预估行数 | 包含的 TEST_F |
|--------|---------|---------|--------------|
| `test_cortex_m3_lifecycle.cpp` | 基础生命周期 | ~90 | ResetClearsState, SetPcAndRead, RegisterNames, StepWhileHaltedReturnsError, SpLowBitsMasked |
| `test_cortex_m3_basic.cpp` | 基础指令 + 条件跳转 | ~240 | MovsImm8, AddsReg, SubsImm8, LoopBne, CallChain, PushPop, StrLdr, CmpBneFlags, CbzAndCbnz |
| `test_cortex_m3_faults.cpp` | Fault + 系统指令 + 异常 + 系统寄存器 | ~200 | FetchUnmapped*, IllegalInstruction*, RegisterCount, CpsAndBarrier, SvcEntersException, MrsMsr |
| `test_cortex_m3_advanced.cpp` | 数据处理 + 乘法 + IT + 高级跳转 | ~200 | ExtendAndReverse, LoadStoreWide, SignedDivision, Bitfield, Multiply, AdcSbc, ItBlock, ConditionalWide, Tbb |
| `test_cortex_m3_common.hpp` | 公共 fixture | ~50 | — |

### 实施步骤

1. **提取公共 fixture** — `CortexM3Test` fixture 类 + `step_n()` 辅助函数 → `test_cortex_m3_common.hpp`
2. **创建 4 个测试文件**，按上表分配 TEST_F，各文件 `#include "test_cortex_m3_common.hpp"`
3. **更新 `test/CMakeLists.txt`**：
   ```cmake
   add_executable(test_cortex_m3
       test_cortex_m3_lifecycle.cpp
       test_cortex_m3_basic.cpp
       test_cortex_m3_faults.cpp
       test_cortex_m3_advanced.cpp
   )
   ```
4. **删除原 `test_cortex_m3.cpp`**

---

## 拆分规则

- 保持 `CortexM3CPU` 对外 API 不变，除非该阶段明确修改错误接口。
- 不顺手改指令行为。
- 每次拆分后立刻编译和跑相关测试。
- 新文件必须加入构建，避免依赖 `GLOB_RECURSE` 的隐式行为作为唯一保障。
- 公共 helper 优先保持 private member，不急着暴露新 header。

## 验收

- 单个 `cortex_m3*.cpp` 文件不再超过 700 行。
- `rg -n "execute_16bit|execute_32bit|interrupt_entry|FaultRecord|fetch16" src/arch/arm/cortex_m3` 能看到职责落在对应文件。
- `cmake --build build` 通过。
- `ctest --test-dir build -R "cortex|interrupt" --output-on-failure` 通过。
- 全量 `ctest --test-dir build --output-on-failure` 通过。

## 验证命令

```bash
find src/arch/arm/cortex_m3 -name '*.cpp' -print0 | xargs -0 wc -l | sort -nr
cmake --build build -j$(nproc)
ctest --test-dir build -R "cortex|interrupt" --output-on-failure
ctest --test-dir build --output-on-failure
```

## 涉及文件清单

**新建文件（8 个）：**
- `src/arch/arm/cortex_m3/cortex_m3_thumb16.cpp`
- `src/arch/arm/cortex_m3/cortex_m3_thumb32.cpp`
- `src/arch/arm/cortex_m3/cortex_m3_interrupt.cpp`
- `test/test_cortex_m3_lifecycle.cpp`
- `test/test_cortex_m3_basic.cpp`
- `test/test_cortex_m3_faults.cpp`
- `test/test_cortex_m3_advanced.cpp`
- `test/test_cortex_m3_common.hpp`

**修改文件（2 个）：**
- `src/arch/arm/cortex_m3/cortex_m3.cpp`（删掉被移走的代码）
- `test/CMakeLists.txt`（更新 test_cortex_m3 target）

**删除文件（1 个）：**
- `test/test_cortex_m3.cpp`（拆分后删除）

## 相关文件

| 工作 | 文件 |
|------|------|
| CPU class | `include/arch/arm/cortex_m3/cortex_m3.hpp` |
| 当前大文件 | `src/arch/arm/cortex_m3/cortex_m3.cpp` |
| reset | `src/arch/arm/cortex_m3/cortex_m3_reset.cpp` |
| decode helpers | `include/arch/arm/cortex_m3/thumb_fields.hpp`、`include/arch/arm/cortex_m3/thumb32_fields.hpp` |
| 测试 | `test/test_cortex_m3.cpp` |
| 测试构建 | `test/CMakeLists.txt` |
| 主构建 | `CMakeLists.txt` |
