#pragma once

#include "autogen/arch_details.hpp"

namespace micro_forge::literals {

constexpr addr_t operator""_kb(unsigned long long v) {
    return static_cast<addr_t>(v * 1024);
}

constexpr addr_t operator""_mb(unsigned long long v) {
    return static_cast<addr_t>(v * 1024 * 1024);
}

constexpr addr_t operator""_addr(unsigned long long v) {
    return static_cast<addr_t>(v);
}

}  // namespace micro_forge::literals
