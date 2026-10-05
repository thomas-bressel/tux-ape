cmake_minimum_required(VERSION 3.21)

project(TuxAPE
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Amstrad CPC / Plus emulator modelled on WinAPE"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TUXAPE_BUILD_APP "Build the Qt application" ON)
option(TUXAPE_BUILD_TESTS "Build the test programs" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The emulation core has no dependency on Qt or on the host system, so it can
# be tested headless and reused by other front ends.
add_subdirectory(src/core)

if(TUXAPE_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(TUXAPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
