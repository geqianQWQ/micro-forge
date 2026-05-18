#include <gtest/gtest.h>

#include "periph/systick.hpp"

using namespace micro_forge;
using namespace micro_forge::periph;

class SysTickTest : public ::testing::Test {
  protected:
    NvicPeripheral nvic_;
    std::unique_ptr<SysTickPeripheral> systick_;

    void SetUp() override {
        systick_ = std::make_unique<SysTickPeripheral>(nvic_);
    }
};

TEST_F(SysTickTest, CountDownToZero) {
    ASSERT_TRUE(systick_->write(0x04, 100, Width::Word).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00, 0x1, Width::Word)
                    .has_value()); // ENABLE → VAL=100

    systick_->tick(100);

    auto ctrl = systick_->read(0x00, Width::Word);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_TRUE(*ctrl & (1u << 16)); // COUNTFLAG set

    auto val = systick_->read(0x08, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 100u); // Auto-reloaded to LOAD value
}

TEST_F(SysTickTest, CountDownPartial) {
    ASSERT_TRUE(systick_->write(0x04, 100, Width::Word).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00, 0x1, Width::Word)
                    .has_value()); // ENABLE → VAL=100

    systick_->tick(50);

    auto ctrl = systick_->read(0x00, Width::Word);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_FALSE(*ctrl & (1u << 16)); // COUNTFLAG not set

    auto val = systick_->read(0x08, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 50u);
}

TEST_F(SysTickTest, TickIntTriggersNvic) {
    ASSERT_TRUE(systick_->write(0x04, 100, Width::Word).has_value());
    ASSERT_TRUE(systick_->write(0x08, 100, Width::Word).has_value());
    ASSERT_TRUE(systick_->write(0x00, 0x3, Width::Word)
                    .has_value()); // ENABLE + TICKINT

    ASSERT_TRUE(nvic_.write(0x000, (1u << 15), Width::Word).has_value());

    systick_->tick(100);

    EXPECT_TRUE(nvic_.has_pending_irq());
    EXPECT_EQ(nvic_.highest_pending_irq(), 15);
}

TEST_F(SysTickTest, MultipleZeroCrossings) {
    ASSERT_TRUE(systick_->write(0x04, 10, Width::Word).has_value());
    ASSERT_TRUE(systick_->write(0x08, 10, Width::Word).has_value());
    ASSERT_TRUE(systick_->write(0x00, 0x3, Width::Word).has_value());
    ASSERT_TRUE(nvic_.write(0x000, (1u << 15), Width::Word).has_value());

    // 35 ticks: 10→0 (fire), reload 10→0 (fire), reload 10→5 (no fire)
    systick_->tick(35);

    auto ctrl = systick_->read(0x00, Width::Word);
    ASSERT_TRUE(ctrl.has_value());
    EXPECT_TRUE(*ctrl & (1u << 16)); // COUNTFLAG set

    auto val = systick_->read(0x08, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 5u); // 35 - 10 - 10 - 10 = 5 remaining
}

TEST_F(SysTickTest, DisabledNoDecrement) {
    ASSERT_TRUE(systick_->write(0x04, 100, Width::Word).has_value()); // LOAD
    ASSERT_TRUE(systick_->write(0x00, 0x1, Width::Word)
                    .has_value()); // ENABLE → VAL=100
    ASSERT_TRUE(systick_->write(0x00, 0x0, Width::Word).has_value()); // Disable

    systick_->tick(100);

    auto val = systick_->read(0x08, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 100u); // Unchanged — disabled
}

TEST_F(SysTickTest, WriteValClears) {
    ASSERT_TRUE(systick_->write(0x00, 0x1, Width::Word).has_value());
    ASSERT_TRUE(systick_->write(0x08, 999, Width::Word).has_value());

    auto val = systick_->read(0x08, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0u);
}

TEST_F(SysTickTest, LoadMasksTo24Bits) {
    ASSERT_TRUE(systick_->write(0x04, 0x01FFFFFF, Width::Word).has_value());

    auto load = systick_->read(0x04, Width::Word);
    ASSERT_TRUE(load.has_value());
    EXPECT_EQ(*load, 0x00FFFFFFu); // Masked to 24 bits
}
