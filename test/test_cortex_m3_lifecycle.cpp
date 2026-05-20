#include "test_cortex_m3_common.hpp"

// ── Lifecycle ──

TEST_F(CortexM3Test, ResetClearsState) {
    reset_cpu();
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Halted);
    EXPECT_EQ(cpu_->cycles().value_or(0xFFFF), 0u);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(reg(i), 0u) << "R" << i << " not zeroed";
    }
}

TEST_F(CortexM3Test, SetPcAndRead) {
    reset_cpu();
    set_pc(0x100);
    EXPECT_EQ(cpu_->pc().value_or(0), 0x100u);
}

TEST_F(CortexM3Test, RegisterNames) {
    reset_cpu();
    for (size_t i = 0; i < 16; ++i) {
        auto name = cpu_->register_name(i);
        ASSERT_TRUE(name.has_value());
    }
    auto bad = cpu_->register_name(16);
    EXPECT_FALSE(bad.has_value());
}

TEST_F(CortexM3Test, StepWhileHaltedReturnsError) {
    reset_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::NotRunning);
}

TEST_F(CortexM3Test, SpLowBitsMasked) {
    reset_cpu();
    set_reg(13, 0x20000003);
    EXPECT_EQ(reg(13), 0x20000000u);
}
