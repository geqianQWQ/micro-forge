if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/STM32CubeF1/Drivers")
    message(STATUS "STM32CubeF1 submodule not found, skipping hal_uart")
    return()
endif()

add_subdirectory(firmware)

add_executable(hal_uart_runner runner.cpp)
target_link_libraries(hal_uart_runner PRIVATE micro_forge)
add_dependencies(hal_uart_runner hal_uart_firmware)

add_custom_command(TARGET hal_uart_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hal_uart.elf
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hal_uart.elf
)
