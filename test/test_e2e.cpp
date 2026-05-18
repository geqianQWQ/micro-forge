#include <gtest/gtest.h>

#include "chips/stm32f1/stm32f103_soc.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), {}};
}

} // anonymous namespace

TEST(E2E, HelloWorld) {
    auto data = read_file(E2E_HELLO_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_HELLO_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    std::string output;
    (*soc)->parts().serial().set_output([&](uint8_t ch) {
        output += static_cast<char>(ch);
    });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    (*soc)->run(100000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted)
        << "CPU faulted during execution";

    EXPECT_NE(output.find("Hello"), std::string::npos)
        << "Output was: " << output;
}

TEST(E2E, GpioBlink) {
    auto data = read_file(E2E_BLINK_ELF);
    ASSERT_FALSE(data.empty()) << "Firmware not found at " E2E_BLINK_ELF;

    auto soc = Stm32f103Soc::create();
    ASSERT_TRUE(soc.has_value());

    int toggle_count = 0;
    (*soc)->parts().gpioa.set_pin_change_callback([&](uint8_t pin, bool) {
        if (pin == 5) toggle_count++;
    });

    auto r = (*soc)->load_elf(data);
    ASSERT_TRUE(r.has_value()) << r.error();

    (*soc)->run(4000000);

    auto state = (*soc)->machine().cpu->state();
    ASSERT_TRUE(state.has_value());
    ASSERT_NE(*state, cpu::CPU::State::Faulted)
        << "CPU faulted during execution";

    EXPECT_GE(toggle_count, 6)
        << "Expected at least 6 PA5 toggles, got " << toggle_count;
}
