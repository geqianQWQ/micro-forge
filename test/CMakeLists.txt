include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# ── 基础设施测试（types + memory） ──
add_executable(test_infra
    test_types.cpp
    test_flat_memory.cpp
    test_memory_bus.cpp
)
target_link_libraries(test_infra
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_infra)

# ── CPU 抽象层测试（ICore + RegisterFile + ToyCore） ──
add_executable(test_cpu
    test_cpu.cpp
)
target_link_libraries(test_cpu
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_cpu)

# ── ARM Cortex-M3 测试 ──
add_executable(test_cortex_m3
    test_cortex_m3.cpp
)
target_link_libraries(test_cortex_m3
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_cortex_m3)

# ── 芯片/平台测试（具体芯片地址映射等） ──
add_executable(test_chip
    test_stm32f103_map.cpp
)
target_link_libraries(test_chip
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_chip)

# ── 仿真时钟测试 ──
add_executable(test_sim
    test_virtual_clock.cpp
    test_coordinator.cpp
)
target_link_libraries(test_sim
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_sim)

# ── NVIC 测试 ──
add_executable(test_nvic
    test_nvic.cpp
)
target_link_libraries(test_nvic
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_nvic)

# ── SysTick 测试 ──
add_executable(test_systick
    test_systick.cpp
)
target_link_libraries(test_systick
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_systick)

# ── 中断集成测试 ──
add_executable(test_interrupt
    test_interrupt_roundtrip.cpp
)
target_link_libraries(test_interrupt
    PRIVATE micro_forge GTest::gtest_main
)
gtest_discover_tests(test_interrupt)
