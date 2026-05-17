# Phase 1 接口讲解与 FlatMemory 抽象评审

> 日期: 2026-05-16
> 阶段: Phase 1 (内存子系统)
> 状态: 讨论完成，FlatMemory 实现尚未完成

---

## 接口讲解总结

### Width 枚举 (`core/types.hpp`)

访问宽度，告诉总线"这次操作读/写几个字节"。
- `Byte = 1` (8位), `HalfWord = 2` (16位), `Word = 4` (32位)
- 底层类型 `uint32_t`，可直接 `static_cast` 得到字节数用于越界检查

### BusError 枚举 (`core/types.hpp`)

模拟器错误码，替代真实芯片的 HardFault 异常：
- `Unmapped` — 访问未映射地址
- `Unaligned` — 非对齐访问
- `Fault` — 通用错误（越界等）
- `ReadOnly` — 写入只读区域（如 Flash）

### Expected\<T\> (`core/types.hpp`)

`std::expected<T, BusError>` 的别名。"要么有值，要么有错误"的返回类型。
- `Expected<uint32_t>` — 成功返回值，失败返回 BusError
- `Expected<void>` — 成功无值，失败返回 BusError

### Peripheral\<io_data_t, io_addr_t\> (`peripheral/interface.h`)

所有外设的纯接口（struct），四个虚函数：
- `read(address, width)` — 纯虚，必须实现
- `write(address, data, width)` — 纯虚，必须实现
- `tick(cycles)` — 有默认空实现，简单外设可不管
- `name()` — 纯虚，调试用

当前为模板，默认 `uint32_t/uint32_t`。

### FlatMemory\<io_data_t, io_addr_t\> (`memory/flat_memory.h`)

继承 Peripheral，最简单的"一块连续字节数组"外设。
- `read/write` — 按小端序逐字节操作
- `load(offset, span)` — 一次性加载固件二进制到指定偏移（本质是 memcpy）
- 持有 `WeakPtrFactory` 支持安全的弱引用
- **实现状态**：头文件声明完成，`.cpp` 为空，缺 `data_`/`size_` 成员

### MemoryBus (`memory/MemoryBus.hpp`, 设计稿)

地址路由器，解耦 CPU 和外设：
- 内部维护 `regions_` 列表，每项有 `{base, size, WeakPtr<Peripheral>}`
- `map(base, size, weak_ptr)` — 注册外设，检查重叠
- `read/write(addr, width)` — 扫描 regions_ 找到对应外设，算出 offset，委托给外设的 read/write
- CPU 只跟 MemoryBus 打交道，不需要知道具体外设的存在

### WeakPtr / WeakPtrFactory (`util/weak_ptr/`)

非拥有式弱引用，解决"总线引用外设但不应拥有外设"的问题：
- `WeakPtrFactory` 在外设对象内部，生成 `WeakPtr` 小票
- 对象销毁时 Factory 析构，所有 WeakPtr 自动失效返回 nullptr
- 支持协变：`WeakPtr<FlatMemory>` → `WeakPtr<Peripheral>` 自动转换
- `WeakPtrFactory` 必须是最后一个成员（C++ 逆序析构）

---

## FlatMemory 抽象评审结论

| 方面 | 评价 |
|---|---|
| 继承方向 Peripheral → FlatMemory | 正确 |
| 接口设计 (read/write/tick/name) | 正确 |
| WeakPtr 生命周期管理 | 正确 |
| 模板参数化 `<io_data_t, io_addr_t>` | 当前阶段过度设计，建议去掉简化 |
| FlatMemory 实现状态 | 未完成（缺 data_/size_ 成员和 .cpp 实现） |
| WeakPtrFactory 放成员最后 | 正确（析构顺序要求） |

