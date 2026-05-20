#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "util/logger.hpp"

#include <string>

using namespace micro_forge;
using namespace micro_forge::cpu;
using namespace micro_forge::cpu::arm::cortex_m3;

// ── Test fixture ──

class CortexM3Test : public ::testing::Test {
protected:
    static constexpr addr_t kMemBase = 0x00000000;
    static constexpr addr_t kMemSize = 4096;

    memory::FlatMemory mem_{kMemSize};
    memory::Bus bus_;
    std::unique_ptr<CortexM3CPU> cpu_;

    void SetUp() override {
        ASSERT_TRUE(bus_.map(
            memory::region(kMemBase, kMemSize, mem_.GetWeak())).has_value());
        cpu_ = std::make_unique<CortexM3CPU>(bus_.GetWeak());
    }

    void load_program(const std::vector<uint16_t>& insns, addr_t base = 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(insns.data());
        ASSERT_TRUE(
            mem_.load(base, {bytes, insns.size() * sizeof(uint16_t)}).has_value());
    }

    void reset_cpu() { (void)cpu_->reset(); }
    void start_cpu() { cpu_->launch(); }
    void step_cpu()  { (void)cpu_->step(); }

    void set_reg(size_t idx, data_t val) {
        (void)cpu_->set_register_value(idx, val);
    }
    data_t reg(size_t idx) {
        return cpu_->register_value(idx).value_or(0xDEAD);
    }
    void set_pc(addr_t pc) { (void)cpu_->set_pc(pc); }

    void run_until_halt(size_t max_steps = 1000) {
        for (size_t i = 0; i < max_steps; ++i) {
            auto res = cpu_->step();
            if (!res.has_value()) break;
            auto st = cpu_->state();
            if (!st.has_value() || *st != CPU::State::Running) break;
        }
    }
};

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
        0x2000,   // 0x00
        0x210A,   // 0x02
        0x3001,   // 0x04 loop:
        0x3901,   // 0x06
        0xD1FC,   // 0x08 bne loop
        0xE001,   // 0x0A
        0xE7FE,   // 0x0C
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
        0x2003,   // 0x00
        0x2104,   // 0x02
        0xF000,   // 0x04 hw1 (BL)
        0xF802,   // 0x06 hw2
        0xE001,   // 0x08
        0xE7FE,   // 0x0A
        0x1840,   // 0x0C add_func:
        0x4770,   // 0x0E bx lr
        0xE7FE,   // 0x10
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
        0x242A,   // 0x00 movs r4, #42
        0xB410,   // 0x02 push {r4}
        0x2400,   // 0x04 movs r4, #0
        0xBC10,   // 0x06 pop  {r4}
        0xE7FE,   // 0x08 hang
        0xE7FE,   // 0x0A hang
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
        0x202A,   // 0x00 movs r0, #42
        0x2100,   // 0x02 movs r1, #0
        0x6048,   // 0x04 str r0, [r1, #4]
        0x2000,   // 0x06 movs r0, #0
        0x6848,   // 0x08 ldr r0, [r1, #4]
        0xE7FE,   // 0x0A hang
        0xE7FE,   // 0x0C hang
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
        0x2005,   // 0x00 movs r0, #5
        0x2805,   // 0x02 cmp r0, #5
        0xD000,   // 0x04 beq hit (offset=0)
        0x2000,   // 0x06 movs r0, #0 (skipped)
        0xE7FE,   // 0x08 hit: b hang
        0xE7FE,   // 0x0A hang
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
        0x2000,   // 0x00
        0xB108,   // 0x02 cbz r0, 0x08
        0x2163,   // 0x04
        0xE000,   // 0x06 b 0x0A
        0x2107,   // 0x08 hit0
        0x2201,   // 0x0A
        0xB90A,   // 0x0C cbnz r2, 0x12
        0x2363,   // 0x0E
        0xE000,   // 0x10 b 0x14
        0x2309,   // 0x12 hit1
        0xE7FE,   // 0x14 hang
    });
    reset_cpu();
    set_reg(13, 0x200);
    start_cpu();
    run_until_halt(20);
    EXPECT_EQ(reg(1), 7u);
    EXPECT_EQ(reg(3), 9u);
    EXPECT_EQ(reg(13), 0x200u);
}

// ── Fetch unmapped fault ──

TEST_F(CortexM3Test, FetchUnmappedFaults) {
    reset_cpu();
    start_cpu();
    set_pc(5000); // Outside 4096-byte memory
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::InstructionFetchFault);
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

// ── Illegal instruction fault ──

TEST_F(CortexM3Test, IllegalInstructionFaults) {
    // 0b01001_xxxx_xxxx_xxxx with op=0b11 is BX
    // Use a genuinely unassigned encoding: 0b11011_xxxx (not handled by decoder)
    load_program({0xDE00}); // decode_key = 0b11011 — unimplemented
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), CPU::CPUError::IllegalInstruction);
    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, CPU::State::Faulted);
}

// ── FaultRecord ──

TEST_F(CortexM3Test, FetchUnmappedRecordsFault) {
    reset_cpu();
    start_cpu();
    set_pc(5000);
    auto res = cpu_->step();
    ASSERT_FALSE(res.has_value());
    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->pc, 5000u);
    EXPECT_EQ(fr->kind, CPU::CPUError::InstructionFetchFault);
}

TEST_F(CortexM3Test, IllegalInstructionRecordsFault) {
    load_program({0xDE00});
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    ASSERT_FALSE(res.has_value());
    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->opcode16, 0xDE00u);
    EXPECT_EQ(fr->kind, CPU::CPUError::IllegalInstruction);
    EXPECT_FALSE(fr->is_32bit);
}

TEST_F(CortexM3Test, IllegalInstructionEmitsFaultLog) {
    std::vector<std::string> messages;
    micro_forge::util::set_log_sink(
        [&](micro_forge::util::LogLevel level, std::string_view module,
            std::string_view message) {
            if (level == micro_forge::util::LogLevel::Error &&
                module == "fault") {
                messages.emplace_back(message);
            }
        });

    load_program({0xDE00});
    reset_cpu();
    start_cpu();
    auto res = cpu_->step();
    micro_forge::util::reset_log_sink();

    ASSERT_FALSE(res.has_value());
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_NE(messages[0].find("PC=0x00000000"), std::string::npos);
    EXPECT_NE(messages[0].find("opcode=0xDE00"), std::string::npos);
    EXPECT_NE(messages[0].find("kind=IllegalInstruction"), std::string::npos);
}

// ── Register count ──

TEST_F(CortexM3Test, RegisterCount) {
    auto cnt = cpu_->register_count();
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, 16u);
}

TEST_F(CortexM3Test, CpsAndBarrierInstructions) {
    load_program({
        0xB672,       // cpsid i
        0xF3EF, 0x8010, // mrs r0, primask
        0xF3BF, 0x8F5F, // dmb sy
        0xF3BF, 0x8F4F, // dsb sy
        0xF3BF, 0x8F6F, // isb sy
        0xB662,       // cpsie i
        0xF3EF, 0x8110, // mrs r1, primask
    });
    reset_cpu();
    start_cpu();
    for (int i = 0; i < 7; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0u);
}

TEST_F(CortexM3Test, SvcEntersException11AndReturns) {
    ASSERT_TRUE(bus_.map(memory::region(0x08000000, kMemSize, mem_.GetWeak()))
                    .has_value());
    ASSERT_TRUE(mem_.write(11 * 4, 0x00000101u, Width::Word).has_value());
    load_program({0xDF12, 0x2001}, 0x80); // svc #0x12; movs r0, #1
    load_program({0x2007, 0x4770}, 0x100); // movs r0, #7; bx lr

    reset_cpu();
    set_pc(0x08000080);
    set_reg(13, 0x300);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value()); // SVC entry
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x100u);

    ASSERT_TRUE(cpu_->step().has_value()); // handler body
    EXPECT_EQ(reg(0), 7u);
    ASSERT_TRUE(cpu_->step().has_value()); // exception return
    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x08000082u);

    ASSERT_TRUE(cpu_->step().has_value()); // instruction after SVC
    EXPECT_EQ(reg(0), 1u);
}

TEST_F(CortexM3Test, MrsMsrExtendedSystemRegisters) {
    load_program({
        0xF380, 0x8810, // msr primask, r0
        0xF381, 0x8811, // msr basepri, r1
        0xF382, 0x8813, // msr faultmask, r2
        0xF383, 0x8814, // msr control, r3
        0xF384, 0x8808, // msr msp, r4
        0xF385, 0x8809, // msr psp, r5
        0xF386, 0x8800, // msr apsr_nzcvq, r6
        0xF3EF, 0x8010, // mrs r0, primask
        0xF3EF, 0x8111, // mrs r1, basepri
        0xF3EF, 0x8213, // mrs r2, faultmask
        0xF3EF, 0x8314, // mrs r3, control
        0xF3EF, 0x8408, // mrs r4, msp
        0xF3EF, 0x8509, // mrs r5, psp
        0xF3EF, 0x8600, // mrs r6, apsr
    });
    reset_cpu();
    set_reg(0, 1);
    set_reg(1, 0x40);
    set_reg(2, 1);
    set_reg(3, 2);
    set_reg(4, 0x200);
    set_reg(5, 0x240);
    set_reg(6, PSR_N | PSR_C);
    start_cpu();

    for (int i = 0; i < 14; ++i) {
        ASSERT_TRUE(cpu_->step().has_value());
    }
    EXPECT_EQ(reg(0), 1u);
    EXPECT_EQ(reg(1), 0x40u);
    EXPECT_EQ(reg(2), 1u);
    EXPECT_EQ(reg(3), 2u);
    EXPECT_EQ(reg(4), 0x200u);
    EXPECT_EQ(reg(5), 0x240u);
    EXPECT_EQ(reg(6) & (PSR_N | PSR_Z | PSR_C | PSR_V), PSR_N | PSR_C);
    EXPECT_EQ(reg(13), 0x240u);
}

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
        0xF8C3, 0x2010, // str.w r2, [r3, #16]
        0xF8D3, 0x4010, // ldr.w r4, [r3, #16]
        0xF8A5, 0x6008, // strh.w r6, [r5, #8]
        0xF8B5, 0x7008, // ldrh.w r7, [r5, #8]
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
        0xFBB1, 0xF0F2, // udiv r0, r1, r2
        0xFB94, 0xF3F5, // sdiv r3, r4, r5
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
        0xF361, 0x200F, // bfi r0, r1, #8, #8
        0xF36F, 0x120F, // bfc r2, #4, #12
        0xF3C4, 0x1345, // ubfx r3, r4, #5, #6
        0xF346, 0x15C7, // sbfx r5, r6, #7, #8
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
        0xFB01, 0x3002, // mla r0, r1, r2, r3
        0xFB05, 0x7416, // mls r4, r5, r6, r7
        0xFBA2, 0x0103, // umull r0, r1, r2, r3
        0xFB86, 0x4507, // smull r4, r5, r6, r7
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
        0xF386, 0x8800, // msr apsr_nzcvq, r6
        0xF141, 0x0001, // adc.w r0, r1, #1
        0xF163, 0x0201, // sbc.w r2, r3, #1
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
        0x2800,       // cmp r0, #0 -> Z=1
        0xF000, 0x8001, // beq.w +2 halfwords -> target at 0x08
        0x2101,       // movs r1, #1 (skipped)
        0x2202,       // movs r2, #2
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
        0xE8DF, 0xF000, // tbb [pc, r0]
        0x0402,         // table bytes: r0=0 -> 0x08, r0=1 -> 0x0C
        0x2163,         // movs r1, #99 (skipped)
        0x2101,         // target0: movs r1, #1
        0xE000,         // b done
        0x2102,         // target1: movs r1, #2
        0xBF00,         // done: nop
    });
    reset_cpu();
    set_reg(0, 1);
    start_cpu();

    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(cpu_->pc().value_or(0), 0x0Cu);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_EQ(reg(1), 2u);
}
