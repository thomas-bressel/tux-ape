cmake_minimum_required(VERSION 3.21)

project(TuxAPE
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Amstrad CPC / Plus emulator modelled on WinAPE"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TUXAPE_BUILD_APP "Build the Qt application" ON)
option(TUXAPE_BUILD_TESTS "Build the test programs" ON)
option(TUXAPE_UPDATE_DIST "Bring dist/ up to date each time the program is built (Linux)" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    # The sources are UTF-8; the standard as it is written; windows.h's
    # min and max macros left out.
    add_compile_options(/W3 /utf-8 /permissive- /Zc:__cplusplus /bigobj)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# The emulation core has no dependency on Qt or on the host system, so it can
# be tested headless and reused by other front ends.
add_subdirectory(src/core)
add_subdirectory(src/tools)

if(TUXAPE_BUILD_APP)
    # The plugins' signatures, and the tool their authors sign them with.
    add_subdirectory(src/sign)
    add_subdirectory(src/app)
endif()

if(TUXAPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
