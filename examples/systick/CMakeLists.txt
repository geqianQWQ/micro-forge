add_subdirectory(firmware)

if(TARGET systick_firmware)
    add_executable(systick_runner runner.cpp)
    target_link_libraries(systick_runner PRIVATE micro_forge)
    add_dependencies(systick_runner systick_firmware)

    add_custom_command(TARGET systick_runner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/systick.elf
            ${CMAKE_CURRENT_BINARY_DIR}/systick.elf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_BINARY_DIR}/firmware/systick.bin
            ${CMAKE_CURRENT_BINARY_DIR}/systick.bin
    )
endif()
