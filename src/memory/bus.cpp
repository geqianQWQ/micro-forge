#include "memory/bus.hpp"

namespace micro_forge::memory {

Expected<void> Bus::map(Region region) {
    if (!region.device.IsValid())
        return std::unexpected(BusError::Fault);

    for (const auto& existing : regions_) {
        if (is_overlap(region, existing)) {
            return std::unexpected(BusError::Fault);
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

Expected<data_t> Bus::read(addr_t addr, Width w) {
    auto* region = find_region(addr);
    if (!region)
        return std::unexpected(BusError::Unmapped);
    if (!region->device.IsValid())
        return std::unexpected(BusError::Fault);
    return region->device->read(addr - region->start, w);
}

Expected<void> Bus::write(addr_t addr, data_t data, Width w) {
    auto* region = find_region(addr);
    if (!region)
        return std::unexpected(BusError::Unmapped);
    if (!region->device.IsValid())
        return std::unexpected(BusError::Fault);
    return region->device->write(addr - region->start, data, w);
}

} // namespace micro_forge::memory
