# Phase 6.0e · Cortex-M3 File Split

## 目标

把膨胀的 `src/arch/arm/cortex_m3/cortex_m3.cpp` 拆成更小的职责单元，让指令补齐、fault 语义、日志迁移可以独立推进。

首批目标不是重写 CPU，而是机械拆分和建立边界。

## 建议拆分

| 新文件 | 内容 |
|--------|------|
| `cortex_m3_core.cpp` | reset、公共 CPU interface、register access、cycles/state |
| `cortex_m3_fetch.cpp` | fetch16、PC advance、instruction fetch fault 处理 |
| `cortex_m3_thumb16.cpp` | 16-bit Thumb decode/execute |
| `cortex_m3_thumb32.cpp` | 32-bit Thumb-2 decode/execute |
| `cortex_m3_exception.cpp` | exception entry/return、SysTick/SVC/HardFault |
| `cortex_m3_fault.cpp` | FaultRecord、fault kind 映射、诊断日志 |

如果一次拆太大，可以先拆 `exception` 和 `fault`，再拆 instruction decode。

## 拆分规则

- 保持 `CortexM3CPU` 对外 API 不变，除非该阶段明确修改错误接口。
- 不顺手改指令行为。
- 每次拆分后立刻编译和跑相关测试。
- 新文件必须加入构建，避免依赖 `GLOB_RECURSE` 的隐式行为作为唯一保障。
- 公共 helper 优先保持 private member，不急着暴露新 header。

## 验收

- 单个 `cortex_m3*.cpp` 文件不再超过 700 行。
- `src/arch/arm/cortex_m3/cortex_m3.cpp` 若保留，应只做薄入口或被完全拆空删除。
- `rg -n "execute_16bit|execute_32bit|interrupt_entry|FaultRecord|fetch16" src/arch/arm/cortex_m3` 能看到职责落在对应文件。
- `cmake --build build` 通过。
- `ctest --test-dir build -R "cortex|interrupt" --output-on-failure` 通过。
- 全量 `ctest --test-dir build --output-on-failure` 通过。

## 建议验证命令

```bash
find src/arch/arm/cortex_m3 -name '*.cpp' -print0 | xargs -0 wc -l | sort -nr
cmake --build build
ctest --test-dir build -R "cortex|interrupt" --output-on-failure
ctest --test-dir build --output-on-failure
```

## 相关文件

| 工作 | 文件 |
|------|------|
| CPU class | `include/arch/arm/cortex_m3/cortex_m3.hpp` |
| 当前大文件 | `src/arch/arm/cortex_m3/cortex_m3.cpp` |
| reset | `src/arch/arm/cortex_m3/cortex_m3_reset.cpp` |
| decode helpers | `include/arch/arm/cortex_m3/thumb_fields.hpp`、`include/arch/arm/cortex_m3/thumb32_fields.hpp` |
| 构建 | `CMakeLists.txt` |
