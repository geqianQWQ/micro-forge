#include "test_cortex_m3_common.hpp"

TEST_F(CortexM3Test, ExtendAndReverseInstructions) {
    load_program({
        0xB248, // sxtb r0, r1
        0xB21A, // sxth r2, r3
        0xB2EC, // uxtb r4, r5
        0xB2BE, // uxth r6, r7
        0xBA08, // rev r0, r1
        0xBA5A, // rev16 r2, r3
        0xBAEC, // revsh r4, r5
    });
    reset_cpu();
    set_reg(1, 0x12345680);
    set_reg(3, 0x00008001);
    set_reg(5, 0x0000AB80);
    set_reg(7, 0x1234FEDC);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0xFFFFFF80u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(2), 0xFFFF8001u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 0x80u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(6), 0xFEDCu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 0x80563412u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(2), 0x00000180u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 0xFFFF80ABu);
}

TEST_F(CortexM3Test, LoadStoreWideWordAndHalfwordImmediateOffsets) {
    load_program({
        0xF8C3,
        0x2010, // str.w r2, [r3, #16]
        0xF8D3,
        0x4010, // ldr.w r4, [r3, #16]
        0xF8A5,
        0x6008, // strh.w r6, [r5, #8]
        0xF8B5,
        0x7008, // ldrh.w r7, [r5, #8]
    });
    reset_cpu();
    set_reg(2, 0x12345678);
    set_reg(3, 0x200);
    set_reg(5, 0x240);
    set_reg(6, 0xABCD);
    start_cpu();

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(4), 0x12345678u);
    EXPECT_EQ(reg(7), 0xABCDu);
}

TEST_F(CortexM3Test, SignedDivisionUsesSignedOperands) {
    load_program({
        0xFBB1,
        0xF0F2, // udiv r0, r1, r2
        0xFB94,
        0xF3F5, // sdiv r3, r4, r5
    });
    reset_cpu();
    set_reg(1, 10);
    set_reg(2, 3);
    set_reg(4, static_cast<data_t>(-9));
    set_reg(5, 2);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 3u);
    EXPECT_EQ(reg(3), static_cast<data_t>(-4));
}

TEST_F(CortexM3Test, BitfieldInstructions) {
    load_program({
        0xF361,
        0x200F, // bfi r0, r1, #8, #8
        0xF36F,
        0x120F, // bfc r2, #4, #12
        0xF3C4,
        0x1345, // ubfx r3, r4, #5, #6
        0xF346,
        0x15C7, // sbfx r5, r6, #7, #8
    });
    reset_cpu();
    set_reg(0, 0xFFFF0000);
    set_reg(1, 0xAB);
    set_reg(2, 0xFFFFFFFF);
    set_reg(4, 0x000006A0);
    set_reg(6, 0x00004000);
    start_cpu();

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 0xFFFFAB00u);
    EXPECT_EQ(reg(2), 0xFFFF000Fu);
    EXPECT_EQ(reg(3), 0x35u);
    EXPECT_EQ(reg(5), 0xFFFFFF80u);
}

TEST_F(CortexM3Test, MultiplyAccumulateAndLongMultiply) {
    load_program({
        0xFB01,
        0x3002, // mla r0, r1, r2, r3
        0xFB05,
        0x7416, // mls r4, r5, r6, r7
        0xFBA2,
        0x0103, // umull r0, r1, r2, r3
        0xFB86,
        0x4507, // smull r4, r5, r6, r7
    });
    reset_cpu();
    set_reg(1, 6);
    set_reg(2, 7);
    set_reg(3, 5);
    set_reg(5, 6);
    set_reg(6, static_cast<data_t>(-2));
    set_reg(7, 50);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 47u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), 62u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(0), 35u);
    EXPECT_EQ(reg(1), 0u);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(4), static_cast<data_t>(-100));
    EXPECT_EQ(reg(5), 0xFFFFFFFFu);
}

TEST_F(CortexM3Test, ModifiedImmediateAdcSbcReadCarryFlag) {
    load_program({
        0xF386,
        0x8800, // msr apsr_nzcvq, r6
        0xF141,
        0x0001, // adc.w r0, r1, #1
        0xF163,
        0x0201, // sbc.w r2, r3, #1
    });
    reset_cpu();
    set_reg(1, 5);
    set_reg(3, 5);
    set_reg(6, PSR_C);
    start_cpu();

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 7u);
    EXPECT_EQ(reg(2), 4u);
}

TEST_F(CortexM3Test, ItBlockConditionallyExecutesFollowingInstructions) {
    load_program({
        0x2801, // cmp r0, #1 -> Z=0
        0xBF08, // it eq
        0x2107, // movs r1, #7 (skipped)
        0xBF18, // it ne
        0x2209, // movs r2, #9 (executed)
    });
    reset_cpu();
    set_reg(0, 0);
    start_cpu();

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(1), 0u);
    EXPECT_EQ(reg(2), 9u);
}

TEST_F(CortexM3Test, ConditionalWideBranch) {
    load_program({
        0x2800, // cmp r0, #0 -> Z=1
        0xF000,
        0x8001, // beq.w +2 halfwords -> target at 0x08
        0x2101, // movs r1, #1 (skipped)
        0x2202, // movs r2, #2
    });
    reset_cpu();
    set_reg(0, 0);
    start_cpu();

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(1), 0u);
    EXPECT_EQ(reg(2), 2u);
}

TEST_F(CortexM3Test, TbbUsesPcPlusFourAsBranchBase) {
    load_program({
        0xE8DF,
        0xF000, // tbb [pc, r0]
        0x0402, // table bytes: r0=0 -> 0x08, r0=1 -> 0x0C
        0x2163, // movs r1, #99 (skipped)
        0x2101, // target0: movs r1, #1
        0xE000, // b done
        0x2102, // target1: movs r1, #2
        0xBF00, // done: nop
    });
    reset_cpu();
    set_reg(0, 1);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x0Cu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 2u);
}
