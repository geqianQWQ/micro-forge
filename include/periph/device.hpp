#pragma once

#include "autogen/arch_details.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace micro_forge::periph {

struct Device {
    virtual ~Device() = default;

    virtual Expected<data_t> read(addr_t offset, Width w) = 0;
    virtual Expected<void> write(addr_t offset, data_t data, Width w) = 0;

    virtual void tick(uint64_t /*cycles*/) {}
    virtual std::string_view name() const noexcept = 0;
};

} // namespace micro_forge::periph
