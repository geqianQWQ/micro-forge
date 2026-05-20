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
    OUTPUT blink.elf blink.bin
    COMMAND ${ARM_GCC} -mcpu=cortex-m3 -mthumb -nostdlib
            -T ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
            -o blink.elf
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    COMMAND ${ARM_OBJCOPY} -O binary blink.elf blink.bin
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.c
            ${CMAKE_CURRENT_SOURCE_DIR}/startup.c
            ${CMAKE_CURRENT_SOURCE_DIR}/STM32F103.ld
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Building gpio_blink firmware"
)

add_custom_target(blink_firmware ALL DEPENDS blink.elf blink.bin)
