# Phase 1 实施手册：内存子系统

## 已确认的设计决策

| 编号 | 决策 | 选择 |
|------|------|------|
| D1 | Boot alias (0x00000000) | 同一 FlatMemory 实例映射两次 |
| D2 | Width 参数类型 | `enum class Width { Byte = 1, Half = 2, Word = 4 }` |
| D3 | Width 定义位置 | `types.hpp` |
| D4 | 目录组织 | 按职责拆：`memory/`、`target/`、`util/weak_ptr/` |
| D5 | 小端实现 | bit manipulation（显式位移），非 memcpy |
| D6 | 外设引用安全 | 搬入 cf::WeakPtr，适配到 micro_forge 命名空间 |
| D7 | WeakPtr 集成方式 | 各具体外设持有 WeakPtrFactory，map() 接受 WeakPtr\<IPeripheral\> |
| D8 | Phase 0 接口范围 | 只实现 Phase 1 依赖的：IPeripheral + MemoryBus；ICore/RegisterFile 留 Phase 2 |

---

## 目标目录结构

```
include/
  core/
    types.hpp                    # 修改：加 enum class Width
    IPeripheral.hpp              # 新建
  memory/
    FlatMemory.hpp               # 新建
    MemoryBus.hpp                # 新建
  target/
    stm32f103.hpp                # 新建
  util/
    weak_ptr/                    # 新建，从 cf:: 适配
      weak_ptr.h
      weak_ptr_factory.h
      private/
        weak_ptr_internals.h
    logger.hpp                   # 已有
src/
  memory/
    FlatMemory.cpp               # 新建
    MemoryBus.cpp                # 新建
test/
  CMakeLists.txt                 # 修改：加新测试
  test_flat_memory.cpp           # 新建
  test_memory_bus.cpp            # 新建
  test_stm32f103_map.cpp         # 新建
```

---

## 实施步骤

### Step 0：适配 WeakPtr

把 `~/CFDesktop/base/include/base/weak_ptr/` 的三个文件搬入 `include/util/weak_ptr/`，做以下适配：

1. **命名空间**：`cf` → `micro_forge`
2. **include 路径**：调整内部引用（如 `"private/weak_ptr_internals.h"` 改为相对于新位置的路径）
3. **头文件 guard**：可以保留 `#pragma once`，不需要改
4. **去掉冗余注释**：原文件有大量 Doxygen 风格注释，你可以保留也可以精简，看个人偏好

适配完后，在 types.hpp 或一个合适的公共头文件中验证 `#include "util/weak_ptr/weak_ptr.h"` 能编译通过。

**验证点**：写一个小测试确认 WeakPtr 生命周期行为正确（创建 factory → GetWeakPtr → 销毁 owner → WeakPtr::Get() 返回 nullptr）。

---

### Step 1：扩展 types.hpp

在 `include/core/types.hpp` 中加入：

```cpp
enum class Width : uint32_t {
    Byte = 1,
    Half = 2,
    Word = 4,
};
```

位置放在 `BusError` 之前或之后都可以，保持 `Expected<T>` 模板别名不变。

可选：加一个 `constexpr uint32_t to_bytes(Width w)` 辅助函数方便内部使用，但不是必须的——`static_cast<uint32_t>(w)` 也很清楚。

**验证点**：现有测试应该仍然通过（纯加法，不改已有内容）。

---

### Step 2：IPeripheral 接口

新建 `include/core/IPeripheral.hpp`：

```cpp
#pragma once

#include "core/types.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace micro_forge {

struct IPeripheral {
    virtual ~IPeripheral() = default;

    virtual Expected<uint32_t> read(uint32_t offset, Width width) = 0;
    virtual Expected<void>     write(uint32_t offset, uint32_t val, Width width) = 0;

    virtual void tick(uint64_t cycles) { /* 默认空实现 */ }
    virtual std::string_view name() const = 0;
};

}  // namespace micro_forge
```

**注意点**：
- `read` 返回 `Expected<uint32_t>`（即 `std::expected<uint32_t, BusError>`），高位始终清零
- `write` 返回 `Expected<void>`（即 `std::expected<void, BusError>`）
- `tick` 有默认空实现，非纯虚——简单外设（如 FlatMemory）不需要实现它
- `offset` 是相对于外设基地址的偏移，不是绝对地址

**验证点**：编译检查——写一个空测试确认 `#include "core/IPeripheral.hpp"` 通过。

---

### Step 3：FlatMemory

#### 头文件 `include/memory/FlatMemory.hpp`

```cpp
#pragma once

#include "core/IPeripheral.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace micro_forge {

class FlatMemory : public IPeripheral {
public:
    explicit FlatMemory(uint32_t size);

    // IPeripheral
    Expected<uint32_t> read(uint32_t offset, Width width) override;
    Expected<void>     write(uint32_t offset, uint32_t val, Width width) override;
    std::string_view   name() const override { return "FlatMemory"; }

    // 固件加载
    Expected<void> load(   brtq  );

    // WeakPtr 支持
    WeakPtr<FlatMemory> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

    // 调试用
    uint32_t size() const { return size_; }

private:
    std::vector<uint8_t> data_;
    uint32_t size_;

    // WeakPtrFactory 必须是最后一个成员
    WeakPtrFactory<FlatMemory> weak_factory_{this};
};

}  // namespace micro_forge
```

#### 实现文件 `src/memory/FlatMemory.cpp`

**构造函数**：
```cpp
FlatMemory::FlatMemory(uint32_t size)
    : data_(size, 0), size_(size) {}
```

**read 实现——小端 bit manipulation**：

核心逻辑（以 Word 为例）：
```
offset + 4 > size_ → 返回 BusError::Fault
result = data_[offset] | (data_[offset+1] << 8) | (data_[offset+2] << 16) | (data_[offset+3] << 24)
```

Half 和 Byte 同理，只取 2 或 1 个字节。用 `static_cast<uint32_t>` 做位移前的转换避免符号问题。

**write 实现——小端逐字节写入**：

核心逻辑（以 Word 为例）：
```
offset + 4 > size_ → 返回 BusError::Fault
data_[offset]   = val & 0xFF;
data_[offset+1] = (val >> 8) & 0xFF;
data_[offset+2] = (val >> 16) & 0xFF;
data_[offset+3] = (val >> 24) & 0xFF;
```

**load 实现**：
```
offset + data.size() > size_ → BusError::Fault
std::memcpy(data_.data() + offset, data.data(), data.size())
```

**注意点**：
- `WeakPtrFactory` 必须是最后声明的成员——它的析构先于 `data_`，确保 WeakPtr 失效在数据销毁之前
- 构造函数初始化列表中 `data_` 先于 `weak_factory_` 初始化，这是正确的（成员按声明顺序初始化）
- `read`/`write` 的 boundary check 用 `offset + width_bytes > size_`，注意 `width_bytes` 是 `static_cast<uint32_t>(width)`

---

### Step 4：MemoryBus

#### 头文件 `include/memory/MemoryBus.hpp`

```cpp
#pragma once

#include "core/IPeripheral.hpp"
#include "core/types.hpp"
#include "util/weak_ptr/weak_ptr.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace micro_forge {

struct MemoryRegion {
    uint32_t base;
    uint32_t size;
    WeakPtr<IPeripheral> peripheral;
};

class MemoryBus {
public:
    Expected<void> map(uint32_t base, uint32_t size, WeakPtr<IPeripheral> p);

    Expected<uint32_t> read(uint32_t addr, Width width);
    Expected<void>     write(uint32_t addr, uint32_t val, Width width);

private:
    std::vector<MemoryRegion> regions_;

    // 内部查找：找到匹配区域返回指针，否则 nullptr
    MemoryRegion* find_region(uint32_t addr);
};

}  // namespace micro_forge
```

#### 实现文件 `src/memory/MemoryBus.cpp`

**map() 实现**：
```
1. 遍历 regions_，检查新区间 [base, base+size) 是否与已有区间重叠
   - 重叠条件：!(new_base >= existing_base + existing_size || existing_base >= new_base + new_size)
   - 重叠 → 返回 BusError::Fault
2. 检查 WeakPtr 是否有效：!p.IsValid() → 返回 BusError::Fault（防御性）
3. regions_.push_back({base, size, std::move(p)})
4. 返回 {}
```

**find_region() 实现**：
```
线性扫描 regions_：
  addr 落入 [region.base, region.base + region.size) → 返回 &region
找不到 → 返回 nullptr
```

**read() 实现**：
```
1. region = find_region(addr)
2. region == nullptr → BusError::Unmapped
3. !region->peripheral.IsValid() → BusError::Fault（外设已死）
4. offset = addr - region->base
5. return region->peripheral->read(offset, width)
```

**write() 实现同理**。

**注意点**：
- `WeakPtr<IPeripheral>` 的 `->` 操作符在你原始实现中带 assert，所以 `region->peripheral->read(...)` 在 IsValid 时安全
- 线性扫描对于模拟器场景性能足够（通常 5-10 个 region）
- `map()` 中 `push_back` 可能导致 `MemoryRegion` 的移动/拷贝，确保 `WeakPtr` 的拷贝语义正确（你的实现支持）

---

### Step 5：STM32F103 配置

新建 `include/target/stm32f103.hpp`：

```cpp
#pragma once

#include "memory/FlatMemory.hpp"
#include "memory/MemoryBus.hpp"

namespace micro_forge {

inline Expected<void> configure_stm32f103(MemoryBus& bus,
                                           FlatMemory& flash,
                                           FlatMemory& sram) {
    // Boot alias：同一 flash 实例，映射到 0x00000000
    auto result = bus.map(0x00000000, 128 * 1024, flash.GetWeakPtr());
    if (!result.has_value()) return result;

    // Flash：128KB @ 0x08000000
    result = bus.map(0x08000000, 128 * 1024, flash.GetWeakPtr());
    if (!result.has_value()) return result;

    // SRAM：20KB @ 0x20000000
    result = bus.map(0x20000000, 20 * 1024, sram.GetWeakPtr());
    if (!result.has_value()) return result;

    return {};
}

}  // namespace micro_forge
```

**注意点**：
- `inline` 函数放在头文件中，避免需要额外的 .cpp 文件
- 外设区域（0x40000000）和 NVIC 区域（0xE0000000）留到 Phase 3B/4 添加
- 调用方负责创建和管理 FlatMemory 对象的生命周期

---

### Step 6：测试

#### test/test_flat_memory.cpp

关键测试用例：
1. **读写一致性**：写 Word → 读 Word，验证值相等
2. **小端验证**：写 `0x12345678` → 读 Byte at offset=0 得 `0x78`，读 Half at offset=0 得 `0x5678`
3. **字节独立性**：写 Byte to offset=5 → 读 offset=4 和 offset=6 不受影响
4. **越界**：offset + width > size → `BusError::Fault`
5. **load + 读回**：`load()` 一段二进制 → 按字节读回验证

#### test/test_memory_bus.cpp

关键测试用例：
1. **单区域路由**：map 一个 FlatMemory → 通过绝对地址读写成功
2. **双区域路由**：map 两个不重叠区域 → 各自正确路由
3. **重叠拒绝**：map 两个重叠区域 → 第二次返回 `BusError::Fault`
4. **未映射地址**：访问未映射地址 → `BusError::Unmapped`
5. **边界测试**：访问区域第一个字节和最后一个字节成功，最后一个字节+1 返回 Unmapped
6. **WeakPtr 失效**（可选）：map 后销毁外设 → read 返回 BusError::Fault

#### test/test_stm32f103_map.cpp

关键测试用例：
1. Flash 区域（0x08000000）读写一致
2. SRAM 区域（0x20000000）读写一致
3. Boot alias（0x00000000）读到 Flash 相同内容
4. 未映射外设区域（0x40000000）→ `BusError::Unmapped`
5. 未映射 NVIC 区域（0xE0000000）→ `BusError::Unmapped`

#### test/CMakeLists.txt 修改

添加新的可执行文件：
```cmake
add_executable(test_flat_memory test_flat_memory.cpp)
target_link_libraries(test_flat_memory PRIVATE GTest::gtest_main micro_forge)
add_test(NAME FlatMemory COMMAND test_flat_memory)

add_executable(test_memory_bus test_memory_bus.cpp)
target_link_libraries(test_memory_bus PRIVATE GTest::gtest_main micro_forge)
add_test(NAME MemoryBus COMMAND test_memory_bus)

add_executable(test_stm32f103_map test_stm32f103_map.cpp)
target_link_libraries(test_stm32f103_map PRIVATE GTest::gtest_main micro_forge)
add_test(NAME STM32F103Map COMMAND test_stm32f103_map)
```

---

## CMake 修改要点

顶层 CMakeLists.txt 需要：
1. 添加 `src/memory/` 到源文件列表（或者用 `file(GLOB_RECURSE)` 收集 `src/**/*.cpp`）
2. 添加 `include/util/weak_ptr/private` 到 include 路径（如果 WeakPtr 内部引用需要）
3. 确保 `target_include_directories` 覆盖所有新增子目录

---

## 验收检查清单

```bash
# 构建
cmake -B build && cmake --build build

# 运行所有测试
ctest --test-dir build --output-on-failure

# 期望结果：
# test_types (已有)          PASS
# test_flat_memory           PASS
# test_memory_bus            PASS
# test_stm32f103_map         PASS
# 零警告
```

---

## 建议的实施顺序

```
Step 0 适配 WeakPtr          → 编译通过
Step 1 扩展 types.hpp        → 旧测试仍绿
Step 2 IPeripheral 接口       → 编译通过
Step 3 FlatMemory            → test_flat_memory 全绿
Step 4 MemoryBus             → test_memory_bus 全绿
Step 5 STM32F103 配置         → test_stm32f103_map 全绿
最终  ctest 全绿 + 零警告
```

每一步做完跑一次 `cmake --build build && ctest --test-dir build`，确认不破坏已有测试。
