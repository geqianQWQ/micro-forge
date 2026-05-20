#include "memory/bus.hpp"

namespace micro_forge::memory {

Expected<void> Bus::map(Region region) {
    if (!region.device.IsValid()) {
        return std::unexpected(BusError::InvalidDevice);
    }

    for (const auto& existing : regions_) {
        if (is_overlap(region, existing)) {
            return std::unexpected(BusError::RegionOverlap);
        }
    }

    regions_.push_back(std::move(region));
    return {};
}

Region* Bus::find_region(addr_t addr) {
    for (auto& r : regions_) {
        if (addr >= r.start && addr < r.end) {
            return &r;
        }
    }
    return nullptr;
}

void Bus::trace_access(bool is_write, addr_t addr, data_t value, Width width,
                       Expected<void> result, std::string_view device) {
    if (!trace_) {
        return;
    }
    BusTraceEvent event{
        .is_write = is_write,
        .addr = addr,
        .value = value,
        .width = width,
        .ok = result.has_value(),
        .error = result.has_value() ? BusError::Unmapped : result.error(),
        .device = device,
    };
    trace_(event);
}

Expected<data_t> Bus::read(addr_t addr, Width w) {
    auto* region = find_region(addr);
    if (!region) {
        trace_access(false, addr, 0, w, std::unexpected(BusError::Unmapped),
                     "unmapped");
        return std::unexpected(BusError::Unmapped);
    }
    if (!region->device.IsValid()) {
        trace_access(false, addr, 0, w, std::unexpected(BusError::InvalidDevice),
                     "invalid");
        return std::unexpected(BusError::InvalidDevice);
    }
    auto result = region->device->read(addr - region->start, w);
    auto trace_result = result.has_value()
                            ? Expected<void>{}
                            : Expected<void>{std::unexpected(result.error())};
    trace_access(false, addr, result.value_or(0), w, trace_result,
                 region->device->name());
    return result;
}

Expected<void> Bus::write(addr_t addr, data_t data, Width w) {
    auto* region = find_region(addr);
    if (!region) {
        trace_access(true, addr, data, w, std::unexpected(BusError::Unmapped),
                     "unmapped");
        return std::unexpected(BusError::Unmapped);
    }
    if (!region->device.IsValid()) {
        trace_access(true, addr, data, w, std::unexpected(BusError::InvalidDevice),
                     "invalid");
        return std::unexpected(BusError::InvalidDevice);
    }
    auto result = region->device->write(addr - region->start, data, w);
    trace_access(true, addr, data, w, result, region->device->name());
    return result;
}

} // namespace micro_forge::memory
