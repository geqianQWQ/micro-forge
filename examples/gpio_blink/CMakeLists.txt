add_subdirectory(firmware)

add_executable(blink_runner runner.cpp)
target_link_libraries(blink_runner PRIVATE micro_forge)
add_dependencies(blink_runner blink_firmware)

add_custom_command(TARGET blink_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/blink.elf
        ${CMAKE_CURRENT_BINARY_DIR}/blink.elf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/blink.bin
        ${CMAKE_CURRENT_BINARY_DIR}/blink.bin
)
