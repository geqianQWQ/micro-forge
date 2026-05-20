#pragma once

#include "autogen/arch_details.hpp"
#include "core/types.hpp"

#include <functional>
#include <string_view>

namespace micro_forge::memory { class Bus; }

namespace micro_forge::tools {

struct MmioAccess {
    bool is_write;
    addr_t addr;
    data_t value;
    Width width;
    bool ok = true;
    BusError error = BusError::Unmapped;
    std::string_view device;
};

using MmioTraceSink = std::function<void(const MmioAccess&)>;

// 启用 Bus 的 MMIO 跟踪，所有读写都会回调到 sink
void enable_mmio_trace(memory::Bus& bus, MmioTraceSink sink);

// 关闭 MMIO 跟踪
void disable_mmio_trace(memory::Bus& bus);

// 格式化一条 MMIO 访问记录
std::string_view format_mmio_access(const MmioAccess& access, char* buf, size_t buf_size);

} // namespace micro_forge::tools
