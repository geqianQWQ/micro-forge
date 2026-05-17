#pragma once

#include "core/types.hpp"
#include "memory/region.hpp"
#include "util/weak_ptr/weak_ptr_factory.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace micro_forge::memory {

class Bus {
public:
    Expected<void> map(Region region);
    Expected<data_t> read(addr_t addr, Width w);
    Expected<void> write(addr_t addr, data_t data, Width w);

    WeakPtr<Bus> GetWeak() { return weak_factory_.GetWeakPtr(); }

private:
    std::vector<Region> regions_;
    Region* find_region(addr_t addr);
    WeakPtrFactory<Bus> weak_factory_{this};
};

}  // namespace micro_forge::memory
