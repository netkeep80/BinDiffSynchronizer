cmake_minimum_required(VERSION 3.16)
project(BinDiffSynchronizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Main library (header-only)
add_library(bindiff INTERFACE)
target_include_directories(bindiff INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Compiler warnings
if(MSVC)
    target_compile_options(bindiff INTERFACE /W4)
else()
    target_compile_options(bindiff INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Tests
enable_testing()
add_subdirectory(tests)

# Experiments / benchmarks (optional, built by default)
add_subdirectory(experiments)
