# Phase 0 · 基础设施

> 预计工期：4-5 天 | 状态：待实施

## 目标

搭建项目骨架：能编译、能测试、所有核心接口定义完毕且语法正确。

---

## 设计决策

### D0-1：CMake + C++23

`cmake_minimum_required(VERSION 3.25)`，`set(CMAKE_CXX_STANDARD 23)`。
启用 `-Wall -Wextra -Werror`，零容忍警告。

### D0-2：GoogleTest via FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)
```

测试放在 `test/` 子目录，独立的 `CMakeLists.txt`，通过 `add_subdirectory` 挂载。

### D0-3：极简日志系统

不引入 spdlog 等外部依赖。一个 `logger.hpp`，宏实现：

```cpp
#define LOG_TRACE(module, msg) ...
#define LOG_DEBUG(module, msg) ...
#define LOG_INFO(module, msg)  ...
#define LOG_WARN(module, msg)  ...
#define LOG_ERROR(module, msg) ...
```

支持按模块过滤（编译期或运行期，先做编译期）。
日志输出到 stderr，格式：`[LEVEL][module] message`。

### D0-4：类型系统

```cpp
// types.hpp
using Addr32  = uint32_t;
using Word32  = uint32_t;
using HalfWord = uint16_t;
using Byte    = uint8_t;

enum class BusError {
    Unmapped,   // 地址无对应外设/内存
    Unaligned,  // 不支持的非对齐访问
    Fault,      // 总线错误
    ReadOnly,   // 写只读区域
};

enum class CoreState {
    Running,    // 正常执行
    Halted,     // HALT 指令后停止
    Faulted,    // 未定义指令/总线错误
};
```

所有错误处理使用 `std::expected<T, BusError>`。

### D0-5：ICore 接口（硬件无关）

```cpp
struct ICore {
    virtual ~ICore() = default;

    // 生命周期
    virtual void        reset()                          = 0;
    virtual void        step()                           = 0;
    virtual CoreState   state()                    const = 0;

    // 寄存器（内部委托给 RegisterFile<N>）
    virtual uint32_t    reg(uint8_t idx)           const = 0;
    virtual void        set_reg(uint8_t idx, uint32_t v) = 0;
    virtual uint8_t     reg_count()                const = 0;
    virtual std::string reg_name(uint8_t idx)      const = 0;

    // PC 快捷方式
    virtual uint32_t    pc()                       const = 0;
    virtual void        set_pc(uint32_t addr)            = 0;

    // 中断
    virtual void        raise_irq(uint8_t irq_n)         = 0;

    // 周期
    virtual uint64_t    cycles()                   const = 0;
};
```

**关键约束**：
- 接口中不出现任何 ISA 特定概念（Thumb/xPSR/EXC_RETURN 等）
- `disasm()` 已移除，反汇编由独立 IDisassembler 组件负责
- CPU 构造时注入 MemoryBus&（不在 ICore 接口上体现）

**step() 语义**：
- `state() == Running` 时执行一条指令
- `state() != Running` 时为 no-op（不抛异常，不执行）
- 每次调用后 `cycles()` 递增

**reg() 语义**：
- idx 范围 `[0, reg_count())`，越界行为未定义（实现可 assert）
- PC 不在 reg() 索引范围内，通过 `pc()/set_pc()` 独立访问
- `reg_name()` 返回人类可读名称，如 "R0"/"A"/"PC"

### D0-6：IPeripheral 接口（多宽度）

```cpp
struct IPeripheral {
    virtual ~IPeripheral() = default;

    virtual std::expected<uint32_t, BusError> read(uint32_t offset, uint32_t width) = 0;
    virtual std::expected<void, BusError>     write(uint32_t offset, uint32_t val, uint32_t width) = 0;

    virtual void tick(uint64_t cycles) {}
    virtual std::string_view name() const = 0;
};
```

**width 语义**：width ∈ {1, 2, 4}，对应 byte/halfword/word。
**offset 语义**：相对于外设基地址的偏移，非绝对地址。MemoryBus 负责地址→偏移转换。
**read 返回值**：始终返回 uint32_t，高位清零（read byte 返回 `val & 0xFF`）。
**tick 语义**：每次 CPU step() 后由调度器调用，传入经过的周期数。默认空实现。

### D0-7：RegisterFile 模板

```cpp
template<size_t N>
struct RegisterFile {
    std::array<uint32_t, N> regs{};
    uint32_t pc{};

    uint32_t get(uint8_t idx) const;
    void     set(uint8_t idx, uint32_t val);
    static constexpr uint8_t count() { return static_cast<uint8_t>(N); }
};
```

- 不虚化，是各 CPU 的内部实现组件
- `idx` 范围检查用 `assert`（debug build）
- 各 CPU 通过组合持有 RegisterFile，ICore 的 `reg()` 委托给它

### D0-8：MemoryBus 骨架

```cpp
struct MemoryBus {
    void map(uint32_t base, uint32_t size, IPeripheral& p);
    std::expected<uint32_t, BusError> read(uint32_t addr, uint32_t width);
    std::expected<void, BusError>     write(uint32_t addr, uint32_t val, uint32_t width);
};
```

- `map()` 注册映射区间，重叠时报错（或覆盖+日志警告）
- `read/write` 将绝对地址转为 `(peripheral, offset)` 并转发
- 本 Phase 只做骨架，完整实现在 Phase 1

---

## 讨论点和待定事项

| # | 问题 | 何时决定 |
|---|------|---------|
| T0-1 | MemoryBus 映射重叠时：报错 vs 覆盖+警告？ | Phase 1 开始时 |
| T0-2 | IPeripheral 的 width 参数用 uint32_t vs enum class Width{Byte, Half, Word}？ | Phase 1 开始时 |
| T0-3 | RegisterFile 的 pc 是否应该独立于 regs 数组？ | Phase 2 开始时（ToyCore 实现时验证） |
| T0-4 | 日志的模块过滤粒度：编译期宏 vs 运行期字符串匹配？ | Phase 0 实现时 |

---

## 隐藏风险

无重大风险。Phase 0 是最安全的阶段，主要工作是定义和编译验证。
唯一风险：如果 ICore 接口在后续 Phase 中发现遗漏（如缺少 `set_pc()`），修复成本低（加一个虚方法）。

---

## 目录结构

```
micro-forge/
├── CMakeLists.txt
├── include/
│   ├── core/
│   │   ├── types.hpp
│   │   ├── ICore.hpp
│   │   ├── IPeripheral.hpp
│   │   ├── MemoryBus.hpp
│   │   └── RegisterFile.hpp
│   └── util/
│       └── logger.hpp
├── src/
│   └── core/
│       └── MemoryBus.cpp
├── test/
│   ├── CMakeLists.txt
│   └── test_types.cpp
├── docs/
│   └── milestones/
└── .clang-format
```

## 实施顺序

1. `git init` + `.gitignore`
2. `CMakeLists.txt`（顶层 + test/ 子目录）
3. `include/core/types.hpp`
4. `include/core/RegisterFile.hpp`
5. `include/core/ICore.hpp`
6. `include/core/IPeripheral.hpp`
7. `include/core/MemoryBus.hpp` + `src/core/MemoryBus.cpp`
8. `include/util/logger.hpp`
9. `test/test_types.cpp`（基础类型编译验证 + RegisterFile 读写测试）
10. 构建验证：`cmake --build build && ctest --test-dir build` 全绿

## 验收标准

- [ ] `cmake -B build && cmake --build build` 零错误零警告
- [ ] `ctest --test-dir build` 通过（至少一个 trivial 测试）
- [ ] `std::expected<uint32_t, BusError>` 编译正确
- [ ] `ICore` 虚接口语法正确
- [ ] `IPeripheral` 多宽度接口语法正确
- [ ] `RegisterFile<8>` 模板实例化正确
