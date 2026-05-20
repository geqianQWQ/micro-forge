#pragma once

#include "core/types.hpp"
#include "memory/region.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <functional>
#include <string_view>
#include <vector>

namespace micro_forge::memory {

struct BusTraceEvent {
    bool is_write;
    addr_t addr;
    data_t value;
    Width width;
    bool ok;
    BusError error;
    std::string_view device;
};

class Bus {
  public:
    using TraceCallback = std::function<void(const BusTraceEvent& event)>;

    Expected<void> map(Region region);
    Expected<data_t> read(addr_t addr, Width w);
    Expected<void> write(addr_t addr, data_t data, Width w);

    void set_trace(TraceCallback cb) { trace_ = std::move(cb); }

    WeakPtr<Bus> GetWeak() { return weak_factory_.GetWeakPtr(); }

  private:
    std::vector<Region> regions_;
    Region* find_region(addr_t addr);
    void trace_access(bool is_write, addr_t addr, data_t value, Width width,
                      Expected<void> result, std::string_view device);
    TraceCallback trace_;
    WeakPtrFactory<Bus> weak_factory_{this};
};

} // namespace micro_forge::memory
