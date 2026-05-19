cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

find_program(ARM_GCC arm-none-eabi-gcc)
if(NOT ARM_GCC)
    find_program(ARM_GCC arm-none-linux-gnueabihf-gcc)
endif()
if(NOT ARM_GCC)
    message(WARNING "No ARM GCC found, skipping firmware build")
    return()
endif()

set(CMAKE_C_COMPILER ${ARM_GCC})

add_custom_command(
    OUTPUT systick.elf systick.bin
    COMMAND ${ARM_GCC} -mcpu=cortex-m3 -mthumb -nostdlib
            -T ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
            -o systick.elf
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    COMMAND arm-none-eabi-objcopy -O binary systick.elf systick.bin
            || true
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.c
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building systick firmware"
)

add_custom_target(systick_firmware ALL DEPENDS systick.elf systick.bin)
