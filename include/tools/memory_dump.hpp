#pragma once

#include "autogen/arch_details.hpp"
#include "core/types.hpp"

#include <functional>
#include <string_view>

namespace micro_forge::memory { class Bus; }

namespace micro_forge::tools {

void memory_dump(memory::Bus& bus, addr_t addr, addr_t size,
                 std::function<void(std::string_view)> sink);

void memory_dump(memory::Bus& bus, addr_t addr, addr_t size);

} // namespace micro_forge::tools
