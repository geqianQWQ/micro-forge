add_subdirectory(firmware)

if(TARGET hello_firmware)
    add_executable(hello_runner runner.cpp)
    target_link_libraries(hello_runner PRIVATE micro_forge)
    add_dependencies(hello_runner hello_firmware)

    add_custom_command(TARGET hello_runner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/hello.elf
            ${CMAKE_CURRENT_BINARY_DIR}/hello.elf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/hello.bin
            ${CMAKE_CURRENT_BINARY_DIR}/hello.bin
    )
endif()
