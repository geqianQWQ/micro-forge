#include <gtest/gtest.h>

#include "autogen/arch_details.hpp"
#include "core/types.hpp"
#include "cpu/cpu.hpp"
#include "util/logger.hpp"

#include <string>
#include <vector>

// -- types.hpp: type alias sizes --

TEST(TypesTest, HalfWordIsTwoBytes) {
    static_assert(sizeof(micro_forge::half_word_t) == 2);
}

TEST(TypesTest, ByteIsOneByte) {
    static_assert(sizeof(micro_forge::byte_t) == 1);
}

// -- autogen/arch_details.hpp: architecture aliases --

TEST(TypesTest, AddrTIsArchWidth) {
    static_assert(sizeof(micro_forge::addr_t) == 4);
}

TEST(TypesTest, DataTIsArchWidth) {
    static_assert(sizeof(micro_forge::data_t) == 4);
}

// -- types.hpp: enum class values --

TEST(TypesTest, BusErrorValues) {
    EXPECT_EQ(micro_forge::BusError::Unmapped,
              static_cast<micro_forge::BusError>(0));
    EXPECT_EQ(micro_forge::BusError::Unaligned,
              static_cast<micro_forge::BusError>(1));
    EXPECT_EQ(micro_forge::BusError::ReadOnly,
              static_cast<micro_forge::BusError>(2));
    EXPECT_EQ(micro_forge::BusError::InvalidDevice,
              static_cast<micro_forge::BusError>(3));
    EXPECT_EQ(micro_forge::BusError::RegionOverlap,
              static_cast<micro_forge::BusError>(4));
    EXPECT_EQ(micro_forge::BusError::OutOfRange,
              static_cast<micro_forge::BusError>(5));
    EXPECT_EQ(micro_forge::BusError::PeripheralFault,
              static_cast<micro_forge::BusError>(6));
}

TEST(TypesTest, CoreStateValues) {
    using S = micro_forge::cpu::CPU::State;
    EXPECT_EQ(S::Running, static_cast<S>(0));
    EXPECT_EQ(S::Halted, static_cast<S>(1));
    EXPECT_EQ(S::Faulted, static_cast<S>(2));
}

// -- types.hpp: std::expected usage --

TEST(TypesTest, ExpectedWithValue) {
    auto ok = micro_forge::Expected<uint32_t>{0x20000000u};
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok.value(), 0x20000000u);
}

TEST(TypesTest, ExpectedWithError) {
    auto err = micro_forge::Expected<uint32_t>{
        std::unexpected(micro_forge::BusError::Unmapped)};
    ASSERT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), micro_forge::BusError::Unmapped);
}

TEST(TypesTest, ExpectedVoidCompiles) {
    auto ok = std::expected<void, micro_forge::BusError>{};
    EXPECT_TRUE(ok.has_value());

    auto err = std::expected<void, micro_forge::BusError>{
        std::unexpected(micro_forge::BusError::ReadOnly)};
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), micro_forge::BusError::ReadOnly);
}

// -- logger.hpp: macros compile --

TEST(LoggerTest, AllLevelsCompile) {
    LOG_TRACE("TestModule", "trace message");
    LOG_DEBUG("TestModule", "debug message");
    LOG_INFO("TestModule", "info message");
    LOG_WARN("TestModule", "warn message");
    LOG_ERROR("TestModule", "error message");
    SUCCEED() << "All LOG macros compiled and executed";
}

TEST(LoggerTest, SinkCanBeReplacedAndCapturesFormattedMessages) {
    struct Entry {
        micro_forge::util::LogLevel level;
        std::string module;
        std::string message;
    };
    std::vector<Entry> entries;

    micro_forge::util::set_log_sink([&](micro_forge::util::LogLevel level,
                                        std::string_view module,
                                        std::string_view message) {
        entries.push_back(
            Entry{level, std::string(module), std::string(message)});
    });

    LOG_INFO("logger-test", "value=%u", 42u);
    LOG_ERROR("logger-test", "kind=%s", "fault");
    micro_forge::util::reset_log_sink();

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].level, micro_forge::util::LogLevel::Info);
    EXPECT_EQ(entries[0].module, "logger-test");
    EXPECT_EQ(entries[0].message, "value=42");
    EXPECT_EQ(entries[1].level, micro_forge::util::LogLevel::Error);
    EXPECT_EQ(entries[1].message, "kind=fault");
}
