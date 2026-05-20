#include <gtest/gtest.h>

#include "chips/stm32f1/stm32f1_rcc.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"

#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;
using namespace micro_forge::memory;

// ── RCC Tests ──

TEST(RccTest, ReadResetValues) {
    Stm32f1Rcc rcc;
    auto cr = rcc.read(0x00, Width::Word);
    ASSERT_TRUE(cr.has_value());
    EXPECT_EQ(*cr, 0x00000083u);

    auto apb2 = rcc.read(0x18, Width::Word);
    ASSERT_TRUE(apb2.has_value());
    EXPECT_EQ(*apb2, 0u);
}

TEST(RccTest, WriteApb2Enable) {
    Stm32f1Rcc rcc;
    ASSERT_TRUE(rcc.write(0x18, 0x00000004, Width::Word).has_value());
    auto val = rcc.read(0x18, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004u);
}

TEST(RccTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Rcc rcc;
    ASSERT_TRUE(bus.map(region(0x40021000, 0x400, rcc.GetWeak())).has_value());

    // Write APB2ENR via bus
    ASSERT_TRUE(bus.write(0x4002'1018, 0x00000004, Width::Word).has_value());
    auto val = bus.read(0x4002'1018, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x00000004u);
}

TEST(RccTest, InvalidOffset) {
    Stm32f1Rcc rcc;
    auto r = rcc.read(0x20, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::PeripheralFault);
}

TEST(RccTest, ClockEnabledCheck) {
    Stm32f1Rcc rcc;
    EXPECT_FALSE(rcc.is_clock_enabled(0x40010800)); // GPIOA not enabled
    rcc.enable_clock(0x40010800);
    EXPECT_TRUE(rcc.is_clock_enabled(0x40010800));
}

// ── GPIO Tests ──

TEST(GpioTest, WriteOdrAndRead) {
    Stm32f1Gpio gpio('A');
    ASSERT_TRUE(gpio.write(0x0C, 0x0020, Width::Word).has_value());
    auto val = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0x0020u);
}

TEST(GpioTest, BsrrSetAndReset) {
    Stm32f1Gpio gpio('A');

    // Set pin 5
    ASSERT_TRUE(gpio.write(0x10, 0x00000020, Width::Word).has_value());
    auto odr = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5) & 1);

    // Reset pin 5 via high 16 bits
    ASSERT_TRUE(gpio.write(0x10, 0x00200000, Width::Word).has_value());
    odr = gpio.read(0x0C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_FALSE((*odr >> 5) & 1);
}

TEST(GpioTest, BsrrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.read(0x10, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::PeripheralFault);
}

TEST(GpioTest, IdrReadOnly) {
    Stm32f1Gpio gpio('A');
    auto r = gpio.write(0x08, 0xFF, Width::Word);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), BusError::ReadOnly);
}

TEST(GpioTest, SimulateInput) {
    Stm32f1Gpio gpio('A');
    gpio.simulate_input(3, true);
    auto idr = gpio.read(0x08, Width::Word);
    ASSERT_TRUE(idr.has_value());
    EXPECT_TRUE((*idr >> 3) & 1);
}

TEST(GpioTest, PinChangeCallback) {
    Stm32f1Gpio gpio('A');
    uint8_t changed_pin = 0xFF;
    bool changed_high = false;
    gpio.set_pin_change_callback([&](uint8_t pin, bool high) {
        changed_pin = pin;
        changed_high = high;
    });

    ASSERT_TRUE(gpio.write(0x0C, 0x0020, Width::Word).has_value()); // pin 5 high
    EXPECT_EQ(changed_pin, 5);
    EXPECT_TRUE(changed_high);
}

TEST(GpioTest, SetPinInterface) {
    Stm32f1Gpio gpio('A');
    gpio.set_pin(3, true);
    EXPECT_TRUE(gpio.get_pin(3));
    gpio.set_pin(3, false);
    EXPECT_FALSE(gpio.get_pin(3));
}

TEST(GpioTest, ConfigurePin) {
    Stm32f1Gpio gpio('A');
    gpio.configure_pin(0, periph::PinMode::Output, periph::PinPull::None,
                       periph::PinSpeed::High);
    auto crl = gpio.read(0x00, Width::Word);
    ASSERT_TRUE(crl.has_value());
    // Pin 0 should have non-zero config (output 50MHz push-pull = 0x3)
    EXPECT_EQ(*crl & 0xF, 0x3u);
}

TEST(GpioTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Gpio gpio('A');
    ASSERT_TRUE(bus.map(region(0x40010800, 0x400, gpio.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x4001'0810, 0x00000020, Width::Word).has_value()); // BSRR set pin5
    auto odr = bus.read(0x4001'080C, Width::Word);
    ASSERT_TRUE(odr.has_value());
    EXPECT_TRUE((*odr >> 5) & 1);
}

// ── USART Tests ──

TEST(UsartTest, WriteDrOutputsChar) {
    Stm32f1Usart usart;
    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(usart.write(0x04, 'H', Width::Word).has_value());
    EXPECT_EQ(captured, 'H');
}

TEST(UsartTest, SrTxeAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00, Width::Word);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x80);
}

TEST(UsartTest, SrTcAlwaysHigh) {
    Stm32f1Usart usart;
    auto sr = usart.read(0x00, Width::Word);
    ASSERT_TRUE(sr.has_value());
    EXPECT_TRUE(*sr & 0x40);
}

TEST(UsartTest, WriteCr1) {
    Stm32f1Usart usart;
    ASSERT_TRUE(usart.write(0x0C, 0x000C, Width::Word).has_value());
    auto cr1 = usart.read(0x0C, Width::Word);
    ASSERT_TRUE(cr1.has_value());
    EXPECT_EQ(*cr1, 0x000Cu);
}

TEST(UsartTest, CanSend) {
    Stm32f1Usart usart;
    EXPECT_TRUE(usart.can_send());
}

TEST(UsartTest, SendByteInterface) {
    Stm32f1Usart usart;
    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });
    usart.send_byte('X');
    EXPECT_EQ(captured, 'X');
}

TEST(UsartTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Usart usart;
    ASSERT_TRUE(bus.map(region(0x40013800, 0x400, usart.GetWeak())).has_value());

    uint8_t captured = 0;
    usart.set_output([&](uint8_t ch) { captured = ch; });

    ASSERT_TRUE(bus.write(0x4001'3804, 'A', Width::Word).has_value());
    EXPECT_EQ(captured, 'A');
}

// ── Timer Tests ──

TEST(TimerTest, TickIncrements) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.enable(true);
    tim.tick(10);
    EXPECT_EQ(tim.counter(), 10u);
}

TEST(TimerTest, Prescaler) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.set_prescaler(9);
    tim.enable(true);
    tim.tick(100);
    EXPECT_EQ(tim.counter(), 10u);
}

TEST(TimerTest, OverflowSetsUif) {
    Stm32f1Timer tim;
    tim.set_auto_reload(10);
    tim.enable(true);
    tim.tick(10);
    EXPECT_TRUE(tim.update_flag());
    EXPECT_EQ(tim.counter(), 0u);
}

TEST(TimerTest, DisabledNoTick) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.enable(false);
    tim.tick(100);
    EXPECT_EQ(tim.counter(), 0u);
}

TEST(TimerTest, TimerInterface) {
    Stm32f1Timer tim;
    tim.set_auto_reload(100);
    tim.set_prescaler(0);
    tim.enable(true);
    tim.tick(50);
    EXPECT_EQ(tim.counter(), 50u);
    EXPECT_FALSE(tim.update_flag());

    tim.tick(60);
    EXPECT_TRUE(tim.update_flag());
    tim.clear_update_flag();
    EXPECT_FALSE(tim.update_flag());
}

TEST(TimerTest, MmioThroughBus) {
    Bus bus;
    Stm32f1Timer tim;
    ASSERT_TRUE(bus.map(region(0x40000000, 0x400, tim.GetWeak())).has_value());

    ASSERT_TRUE(bus.write(0x4000'0000, 0x0001, Width::Word).has_value()); // CR1=CEN
    ASSERT_TRUE(bus.write(0x4000'002C, 100, Width::Word).has_value());    // ARR
    ASSERT_TRUE(bus.write(0x4000'0028, 0, Width::Word).has_value());      // PSC

    tim.tick(10);
    auto cnt = bus.read(0x4000'0034, Width::Word);
    ASSERT_TRUE(cnt.has_value());
    EXPECT_EQ(*cnt, 10u);
}
