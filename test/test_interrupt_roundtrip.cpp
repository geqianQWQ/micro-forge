#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/clock_domains.hpp"
#include "chips/stm32f1/interrupt_config.hpp"
#include "chips/stm32f1/memory_bus.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "periph/systick.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"

using namespace micro_forge;
using namespace micro_forge::cpu::arm::cortex_m3;
using namespace micro_forge::periph;
using namespace micro_forge::sim;
using namespace micro_forge::chips::stm32f1;

// ── Helpers ──

static void store_word(memory::Bus& bus, addr_t addr, uint32_t val) {
    ASSERT_TRUE(bus.write(addr, val, Width::Word).has_value());
}

static void store_halfwords(memory::FlatMemory& mem, addr_t offset,
                            const std::vector<uint16_t>& insns) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(insns.data());
    ASSERT_TRUE(
        mem.load(offset, {bytes, insns.size() * sizeof(uint16_t)}).has_value());
}

class InterruptTest : public ::testing::Test {
  protected:
    static constexpr addr_t kFlashBase = 0x08000000;
    static constexpr addr_t kSramBase = 0x20000000;
    static constexpr uint32_t kInitSp = 0x20005000;

    // Vector table has 32 entries (128 bytes) for exceptions + IRQ 0-15
    // Code starts after vector table at offset 0x100
    static constexpr addr_t kMainCode = kFlashBase + 0x100;
    static constexpr addr_t kHandlerCode = kFlashBase + 0x110;

    memory::FlatMemory flash_{128 * 1024};
    memory::FlatMemory sram_{20 * 1024};
    memory::Bus bus_;
    NvicPeripheral nvic_;
    ScbPeripheral scb_;
    std::unique_ptr<SysTickPeripheral> systick_;
    std::unique_ptr<CortexM3CPU> cpu_;

    void SetUp() override {
        systick_ = std::make_unique<SysTickPeripheral>();
        ASSERT_TRUE(
            chips::stm32f1::configure_memory(bus_, flash_, sram_).has_value());
        ASSERT_TRUE(chips::stm32f1::configure_interrupt_devices(bus_, nvic_,
                                                                *systick_, scb_)
                        .has_value());

        cpu_ = std::make_unique<CortexM3CPU>(bus_.GetWeak());
        cpu_->set_nvic(nvic_);
        scb_.set_vtor_callback(
            [this](uint32_t vtor) { cpu_->set_vector_table_base(vtor); });

        // Wire SysTick callback → CPU system exception
        systick_->set_irq_callback([this]() { cpu_->sys_tick_irq(); });
        ASSERT_TRUE(cpu_->reset().has_value());
        ASSERT_TRUE(cpu_->set_register_value(13, kInitSp).has_value());
        cpu_->launch();
    }

    void store_vector_table_entry(uint32_t index, uint32_t value) {
        store_word(bus_, kFlashBase + index * 4, value);
    }

    void store_instructions(addr_t addr, const std::vector<uint16_t>& insns) {
        store_halfwords(flash_, addr - kFlashBase, insns);
    }
};

// ── Test 1: BX LR return from interrupt ──

TEST_F(InterruptTest, BxLrReturnFromInterrupt) {
    // Vector table
    store_vector_table_entry(0, kInitSp);            // Initial SP
    store_vector_table_entry(1, kMainCode);          // Reset handler
    store_vector_table_entry(16, kHandlerCode | 1u); // IRQ 0 handler

    // Main code: B . (infinite loop, 0xE7FE)
    store_instructions(kMainCode, {0xE7FE});

    // Handler code: BX LR (0x4770)
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    uint32_t orig_sp = cpu_->register_value(13).value();

    // Enable and pend IRQ 0
    ASSERT_TRUE(nvic_.write(0x000, (1u << 0), Width::Word).has_value());
    nvic_.set_pending(0);

    // Step: CPU enters interrupt handler and executes first handler instruction
    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    // Verify handler mode entered
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(13).value(), orig_sp - 32);
    EXPECT_EQ(cpu_->register_value(14).value(), 0xFFFFFFF9u);
    EXPECT_EQ(cpu_->pc().value(), kHandlerCode);

    // Step: BX LR → interrupt return
    step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(13).value(), orig_sp);
    EXPECT_EQ(cpu_->pc().value(), kMainCode);
}

// ── Test 2: Stack frame layout verification ──

TEST_F(InterruptTest, StackFrameLayout) {
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kHandlerCode | 1u);

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());
    ASSERT_TRUE(cpu_->set_register_value(0, 0xAABBCCDDu).has_value());
    ASSERT_TRUE(cpu_->set_register_value(3, 0x11223344u).has_value());
    ASSERT_TRUE(cpu_->set_register_value(12, 0x55667788u).has_value());

    uint32_t orig_sp = cpu_->register_value(13).value();

    ASSERT_TRUE(nvic_.write(0x000, 1u, Width::Word).has_value());
    nvic_.set_pending(0);

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    uint32_t new_sp = cpu_->register_value(13).value();
    ASSERT_EQ(new_sp, orig_sp - 32);

    // Stack frame: SP+0=R0, SP+4=R1, SP+8=R2, SP+C=R3, SP+10=R12,
    //              SP+14=LR, SP+18=PC, SP+1C=xPSR
    auto r0 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x00, Width::Word);
    auto r3 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x0C, Width::Word);
    auto r12 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x10, Width::Word);
    auto ret_pc =
        bus_.read(kSramBase + (new_sp - kSramBase) + 0x18, Width::Word);

    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r3.has_value());
    ASSERT_TRUE(r12.has_value());
    ASSERT_TRUE(ret_pc.has_value());

    EXPECT_EQ(*r0, 0xAABBCCDDu);
    EXPECT_EQ(*r3, 0x11223344u);
    EXPECT_EQ(*r12, 0x55667788u);
    EXPECT_EQ(*ret_pc, kMainCode);
}

// ── Test 3: SysTick roundtrip via coordinator ──

TEST_F(InterruptTest, SysTickRoundtrip) {
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    // SysTick is system exception 15 → vector table entry 15
    store_vector_table_entry(15, kHandlerCode | 1u);

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    // Configure SysTick: LOAD=10, CTRL=ENABLE+TICKINT (no NVIC needed)
    ASSERT_TRUE(bus_.write(0xE000E014, 10, Width::Word).has_value()); // LOAD
    ASSERT_TRUE(bus_.write(0xE000E010, 3, Width::Word).has_value());  // CTRL

    VirtualClock clk(stm32f103_default_clocks);
    SimulationCoordinator coord(std::move(clk));
    coord.set_cpu(cpu_->GetWeak());
    coord.add_tickable(systick_->GetWeak(), domain_index(ClockDomain::Sysclk));

    bool handler_entered = false;
    bool handler_returned = false;

    for (size_t i = 0; i < 30; ++i) {
        auto r = coord.step();
        ASSERT_TRUE(r.has_value());

        if (cpu_->in_handler_mode() && !handler_entered) {
            handler_entered = true;
        }
        if (handler_entered && !cpu_->in_handler_mode()) {
            handler_returned = true;
            break;
        }
    }

    EXPECT_TRUE(handler_entered);
    EXPECT_TRUE(handler_returned);
    EXPECT_FALSE(cpu_->in_handler_mode());
}

// ── Test 4: HardFault reads vector index 3 (not 19) ──

TEST_F(InterruptTest, HardFaultReadsVectorIndex3) {
    uint32_t handler_addr = kFlashBase + 0x200;
    store_vector_table_entry(3, handler_addr | 1u);
    // Trap value at index 19 — if HardFault erroneously reads this index,
    // it would jump to the trap address instead of the real handler.
    store_vector_table_entry(19, 0xDEAD0001u);

    // 0xDE00 = UDF (undefined instruction)
    store_instructions(kMainCode, {0xDE00});
    store_instructions(handler_addr, {0xE7FE}); // B .

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), handler_addr);

    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, cpu::CPU::State::Running);

    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->kind, cpu::CPU::CPUError::IllegalInstruction);
}

// ── Test 5: HardFault vector is 0 → Faulted ──

TEST_F(InterruptTest, HardFaultVectorZeroCausesFaulted) {
    store_vector_table_entry(3, 0);
    store_instructions(kMainCode, {0xDE00});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    EXPECT_FALSE(step_res.has_value());

    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, cpu::CPU::State::Faulted);

    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->kind, cpu::CPU::CPUError::IllegalInstruction);
}

// ── Test 6: VTOR write updates vector base ──

TEST_F(InterruptTest, VtorWriteUpdatesVectorBase) {
    uint32_t new_vtor = kFlashBase + 0x400;
    uint32_t handler_addr = kFlashBase + 0x500;

    // Write new vector table at secondary location: index 3 = HardFault handler
    store_word(bus_, new_vtor + 3 * 4, handler_addr | 1u);

    // Write SCB VTOR (SCB base = 0xE000ED00, VTOR offset = 0x08)
    ASSERT_TRUE(bus_.write(0xE000ED08, new_vtor, Width::Word).has_value());

    // Trigger a fault — should read handler from new VTOR location
    store_instructions(kMainCode, {0xDE00});
    store_instructions(handler_addr, {0xE7FE});
    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), handler_addr);
}

TEST_F(InterruptTest, AircrWrongKeyIsCompatibilityNoOp) {
    auto before = scb_.read(0x0C, Width::Word);
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(scb_.write(0x0C, 0x12340004, Width::Word).has_value());

    auto after = scb_.read(0x0C, Width::Word);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, *before);
}
