add_subdirectory(firmware)

add_executable(hello_runner runner.cpp)
target_link_libraries(hello_runner PRIVATE micro_forge)
add_dependencies(hello_runner hello_firmware)

# Copy firmware to build dir for runner to find
add_custom_command(TARGET hello_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hello.elf
        ${CMAKE_CURRENT_BINARY_DIR}/hello.elf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/firmware/hello.bin
        ${CMAKE_CURRENT_BINARY_DIR}/hello.bin
)
