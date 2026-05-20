cmake_minimum_required(VERSION 3.20)

# Cross-compile firmware for ARM Cortex-M3
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
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)
if(NOT ARM_OBJCOPY)
    find_program(ARM_OBJCOPY arm-none-linux-gnueabihf-objcopy)
endif()
if(NOT ARM_OBJCOPY)
    message(WARNING "No ARM objcopy found, skipping firmware build")
    return()
endif()

set(CMAKE_C_COMPILER ${ARM_GCC})

add_custom_command(
    OUTPUT hello.elf hello.bin
    COMMAND ${ARM_GCC} -mcpu=cortex-m3 -mthumb -nostdlib
            -T ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
            -o hello.elf
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    COMMAND ${ARM_OBJCOPY} -O binary hello.elf hello.bin
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.c
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building hello_world firmware"
)

add_custom_target(hello_firmware ALL DEPENDS hello.elf hello.bin)
