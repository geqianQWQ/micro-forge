#include "test_cortex_m3_common.hpp"

// ── MOVS Rd, imm8 ──

TEST_F(CortexM3Test, MovsImm8) {
    // movs r0, #3    → 0x2003
    // movs r1, #4    → 0x2104
    // b   hang       → 0xE001
    // movs r0, #99   → 0x2063  (skipped)
    // hang: b hang   → 0xE7FE
    load_program({0x2003, 0x2104, 0xE001, 0x2063, 0xE7FE});
    reset_cpu();
    start_cpu();
    run_until_halt(10);
    EXPECT_EQ(reg(0), 3u);
    EXPECT_EQ(reg(1), 4u);
}

// ── ADDS Rd, Rn, Rm ──

TEST_F(CortexM3Test, AddsReg) {
    // movs r0, #5      → 0x2005
    // movs r1, #3      → 0x2103
    // adds r0, r0, r1  → 0x1840
    // b    hang        → 0xE001
    // hang: b hang     → 0xE7FE
    load_program({0x2005, 0x2103, 0x1840, 0xE001, 0xE7FE});
    reset_cpu();
    start_cpu();
    run_until_halt(10);
    EXPECT_EQ(reg(0), 8u);
}

// ── SUBS Rd, imm8 ──

TEST_F(CortexM3Test, SubsImm8) {
    // movs r1, #10  → 0x210A
    // subs r1, #1   → 0x3901
    // b    hang     → 0xE001
    // hang: b hang  → 0xE7FE
    load_program({0x210A, 0x3901, 0xE001, 0xE7FE});
    reset_cpu();
    start_cpu();
    run_until_halt(10);
    EXPECT_EQ(reg(1), 9u);
}

// ── BNE loop ──

TEST_F(CortexM3Test, LoopBne) {
    // movs r0, #0    → 0x2000
    // movs r1, #10   → 0x210A
    // loop:
    // adds r0, #1    → 0x3001  (addr 0x04)
    // subs r1, #1    → 0x3901  (addr 0x06)
    // bne  loop      → 0xD1FC  (addr 0x08, offset=-4 → target=0x04)
    // b    hang      → 0xE001  (addr 0x0A)
    // hang: b hang   → 0xE7FE  (addr 0x0C)
    load_program({
        0x2000, // 0x00
        0x210A, // 0x02
        0x3001, // 0x04 loop:
        0x3901, // 0x06
        0xD1FC, // 0x08 bne loop
        0xE001, // 0x0A
        0xE7FE, // 0x0C
    });
    reset_cpu();
    start_cpu();
    run_until_halt(100);
    EXPECT_EQ(reg(0), 10u);
    EXPECT_EQ(reg(1), 0u);
}

// ── BL + BX LR call chain ──

TEST_F(CortexM3Test, CallChain) {
    // addr 0x00: movs r0, #3     → 0x2003
    // addr 0x02: movs r1, #4     → 0x2104
    // addr 0x04: bl add_func     → hw1=0xF000, hw2=0xF804
    //   offset = target - (pc+4) = 0x0C - (0x04+4) = 4
    //   S=0, imm10=0, J1=1, J2=1, imm11=4/2=2
    //   hw2 = 1_1_1_1_0_1_1_00000000010 = 0xF802
    // addr 0x08: b   hang        → 0xE001
    // addr 0x0A: hang: b hang    → 0xE7FE
    // addr 0x0C: adds r0, r0, r1 → 0x1840
    // addr 0x0E: bx  lr          → 0x4770
    // addr 0x10: hang: b hang    → 0xE7FE
    load_program({
        0x2003, // 0x00
        0x2104, // 0x02
        0xF000, // 0x04 hw1 (BL)
        0xF802, // 0x06 hw2
        0xE001, // 0x08
        0xE7FE, // 0x0A
        0x1840, // 0x0C add_func:
        0x4770, // 0x0E bx lr
        0xE7FE, // 0x10
    });
    reset_cpu();
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(0), 7u); // 3 + 4
}

// ── PUSH / POP ──

TEST_F(CortexM3Test, PushPop) {
    // movs r4, #42   → 0x242A  (r4 = 42)
    // push {r4, lr}  → 0xB510
    // movs r4, #0    → 0x2400
    // pop  {r4, pc}  → 0xBD10
    // b    hang      → 0xE7FE
    // hang: b hang   → 0xE7FE
    //
    // But pop {r4, pc} will load pc from stack, which has LR value.
    // We need LR to point somewhere valid. Let's use a different approach.
    //
    // Simpler: push {r4}, clobber r4, pop {r4}, verify restore
    // movs r4, #42   → 0x242A
    // push {r4}      → 0xB410
    // movs r4, #0    → 0x2400
    // pop  {r4}      → 0xBC10
    // b    hang      → 0xE7FE
    // hang: b hang   → 0xE7FE
    load_program({
        0x242A, // 0x00 movs r4, #42
        0xB410, // 0x02 push {r4}
        0x2400, // 0x04 movs r4, #0
        0xBC10, // 0x06 pop  {r4}
        0xE7FE, // 0x08 hang
        0xE7FE, // 0x0A hang
    });
    reset_cpu();
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(4), 42u);
}

// ── STR / LDR word immediate offset ──

TEST_F(CortexM3Test, StrLdr) {
    // movs r0, #42   → 0x202A  (val to store)
    // movs r1, #0    → 0x2100  (base addr)
    // str  r0, [r1, #4] → 0x6048  (store at addr 4)
    // movs r0, #0    → 0x2000  (clobber)
    // ldr  r0, [r1, #4] → 0x6848  (load from addr 4)
    // b    hang      → 0xE7FE
    // hang: b hang   → 0xE7FE
    //
    // STR Rt, [Rn, #imm5*4]: bits[15:11]=01100, imm5=1 → offset=4
    // 0b01100_00001_001_000 = 0x6048
    // LDR Rt, [Rn, #imm5*4]: bits[15:11]=01101, imm5=1 → offset=4
    // 0b01101_00001_001_000 = 0x6848
    load_program({
        0x202A, // 0x00 movs r0, #42
        0x2100, // 0x02 movs r1, #0
        0x6048, // 0x04 str r0, [r1, #4]
        0x2000, // 0x06 movs r0, #0
        0x6848, // 0x08 ldr r0, [r1, #4]
        0xE7FE, // 0x0A hang
        0xE7FE, // 0x0C hang
    });
    reset_cpu();
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(0), 42u);
}

// ── CMP + BNE (flags) ──

TEST_F(CortexM3Test, CmpBneFlags) {
    // movs r0, #5    → 0x2005
    // cmp  r0, #5    → 0x2805  (5-5=0, Z=1)
    // beq  hit       → 0xD000  (cond=EQ, offset=0 → target=0x08)
    // movs r0, #0    → 0x2000  (skipped if Z=1)
    // hit: b hang    → 0xE7FE
    // hang: b hang   → 0xE7FE
    load_program({
        0x2005, // 0x00 movs r0, #5
        0x2805, // 0x02 cmp r0, #5
        0xD000, // 0x04 beq hit (offset=0)
        0x2000, // 0x06 movs r0, #0 (skipped)
        0xE7FE, // 0x08 hit: b hang
        0xE7FE, // 0x0A hang
    });
    reset_cpu();
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(0), 5u); // r0 not clobbered → beq was taken
}

// ── CBZ / CBNZ ──

TEST_F(CortexM3Test, CbzAndCbnzBranchWithoutTouchingStack) {
    // movs r0, #0      → 0x2000
    // cbz  r0, hit0   → 0xB108  (target 0x08)
    // movs r1, #99    → 0x2163  (skipped)
    // hit0: movs r1, #7
    // movs r2, #1
    // cbnz r2, hit1   → 0xB90A  (target 0x12)
    // movs r3, #99    → 0x2363  (skipped)
    // hit1: movs r3, #9
    // b    hang       → 0xE7FE
    load_program({
        0x2000, // 0x00
        0xB108, // 0x02 cbz r0, 0x08
        0x2163, // 0x04
        0xE000, // 0x06 b 0x0A
        0x2107, // 0x08 hit0
        0x2201, // 0x0A
        0xB90A, // 0x0C cbnz r2, 0x12
        0x2363, // 0x0E
        0xE000, // 0x10 b 0x14
        0x2309, // 0x12 hit1
        0xE7FE, // 0x14 hang
    });
    reset_cpu();
    set_reg(13, 0x200);
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(1), 7u);
    EXPECT_EQ(reg(3), 9u);
    EXPECT_EQ(reg(13), 0x200u);
}
