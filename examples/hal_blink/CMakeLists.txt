if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/STM32CubeF1/Drivers")
    message(STATUS "STM32CubeF1 submodule not found, skipping hal_blink")
    return()
endif()

add_subdirectory(firmware)

add_executable(hal_blink_runner runner.cpp)
target_link_libraries(hal_blink_runner PRIVATE micro_forge)
add_dependencies(hal_blink_runner hal_blink_firmware)

add_custom_command(TARGET hal_blink_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hal_blink.elf
        ${CMAKE_CURRENT_BINARY_DIR}/hal_blink.elf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hal_blink.bin
        ${CMAKE_CURRENT_BINARY_DIR}/hal_blink.bin
)
