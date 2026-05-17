#pragma once
#include "autogen/arch_details.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

namespace micro_forge::cpu::reg {

template <uint16_t reg_count> class Registers {
  public:
    enum class RegisterError { IndexOverflow };
    template <typename T> using RegisterExpected =
        std::expected<T, RegisterError>;

    RegisterExpected<data_t> read(const std::size_t idx) const {
        if (idx >= reg_count) {
            return std::unexpected{RegisterError::IndexOverflow};
        }
        return registers[idx];
    }

    RegisterExpected<void> write(const std::size_t idx, const data_t data) {
        if (idx >= reg_count) {
            return std::unexpected{RegisterError::IndexOverflow};
        }
        registers[idx] = data;
        return {};
    }

    virtual void reset() {
        for (auto& each : registers) {
            each = 0;
        }
    }

    constexpr inline std::size_t size() const noexcept { return reg_count; }

  private:
    std::array<data_t, reg_count> registers;
};
} // namespace micro_forge::cpu::reg
