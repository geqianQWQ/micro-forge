cmake_minimum_required(VERSION 3.25)

project(micro-forge
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MICRO_FORGE_ARCH_BIT 32 CACHE STRING "Target architecture bit width (32 or 64)")
set(ARCH_BIT ${MICRO_FORGE_ARCH_BIT})

add_compile_options(-Wall -Wextra -Werror)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/autogen/arch_details.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/autogen/arch_details.hpp
    @ONLY
)

file(GLOB_RECURSE MICRO_FORGE_SOURCES src/*.cpp)

add_library(micro_forge STATIC ${MICRO_FORGE_SOURCES})
target_include_directories(micro_forge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

enable_testing()
add_subdirectory(test)
