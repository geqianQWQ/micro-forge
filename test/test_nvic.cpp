#include <gtest/gtest.h>

#include "memory/bus.hpp"
#include "periph/nvic.hpp"

using namespace micro_forge;
using namespace micro_forge::periph;

// ── Direct API tests (no bus) ──

TEST(NvicTest, SetPendingNotEnabled) {
    NvicPeripheral nvic;
    nvic.set_pending(5);
    EXPECT_FALSE(nvic.is_enabled(5));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

TEST(NvicTest, EnableAndPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(
        nvic.write(0x000, (1u << 5), Width::Word).has_value()); // ISER0, bit 5
    nvic.set_pending(5);
    EXPECT_TRUE(nvic.has_pending_irq());
    EXPECT_TRUE(nvic.is_enabled(5));
}

TEST(NvicTest, HighestPendingReturnsLowest) {
    NvicPeripheral nvic;
    ASSERT_TRUE(
        nvic.write(0x000, (1u << 5) | (1u << 10), Width::Word).has_value());
    nvic.set_pending(10);
    nvic.set_pending(5);
    EXPECT_EQ(nvic.highest_pending_irq(), 5);
}

TEST(NvicTest, ClearPending) {
    NvicPeripheral nvic;
    ASSERT_TRUE(nvic.write(0x000, (1u << 5), Width::Word).has_value());
    nvic.set_pending(5);
    EXPECT_TRUE(nvic.has_pending_irq());

    nvic.clear_pending(5);
    EXPECT_FALSE(nvic.has_pending_irq());
}

TEST(NvicTest, DisabledIrqNotServiced) {
    NvicPeripheral nvic;
    nvic.set_pending(5);
    EXPECT_FALSE(nvic.is_enabled(5));
    EXPECT_FALSE(nvic.has_pending_irq()); // pending + !enabled = not active
}

// ── MMIO tests (through bus) ──

class NvicMmioTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result =
            bus_.map(memory::region(0xE000E100, 0xC00, nvic_.GetWeak()));
        ASSERT_TRUE(result.has_value());
    }

    memory::Bus bus_;
    NvicPeripheral nvic_;
};

TEST_F(NvicMmioTest, IsenWriteRead) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 3), Width::Word).has_value());

    auto r = bus_.read(0xE000E100, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 3));
}

TEST_F(NvicMmioTest, IcerClearsIsen) {
    ASSERT_TRUE(bus_.write(0xE000E100, 0xFFFFFFFF, Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E180, (1u << 3), Width::Word).has_value());

    auto r = bus_.read(0xE000E100, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r & (1u << 3));
    EXPECT_TRUE(*r & (1u << 0));
}

TEST_F(NvicMmioTest, IsprWriteRead) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E200, (1u << 7), Width::Word).has_value());

    auto r = bus_.read(0xE000E200, Width::Word);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r & (1u << 7));

    EXPECT_TRUE(nvic_.has_pending_irq());
    EXPECT_EQ(nvic_.highest_pending_irq(), 7);
}

TEST_F(NvicMmioTest, IcprClearsIspr) {
    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E200, (1u << 7), Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E280, (1u << 7), Width::Word).has_value());

    EXPECT_FALSE(nvic_.has_pending_irq());
}

TEST_F(NvicMmioTest, PriorityReadWrite) {
    ASSERT_TRUE(bus_.write(0xE000E400,
                           0x20 | (0x40 << 8) | (0x60 << 16) | (0x80 << 24),
                           Width::Word)
                    .has_value());

    EXPECT_EQ(nvic_.irq_priority(0), 0x20);
    EXPECT_EQ(nvic_.irq_priority(1), 0x40);
    EXPECT_EQ(nvic_.irq_priority(2), 0x60);
    EXPECT_EQ(nvic_.irq_priority(3), 0x80);
}
