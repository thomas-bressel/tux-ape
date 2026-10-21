# Hello: the model of a TuxAPE plugin (see PLUGINS.md at TuxAPE's root).
# To make one's own: copy this folder, then put the plugin's id where
# "hello" is, its name where "Hello" is and its upper-case id where "HELLO"
# is, in the files and in their names; and say what it does where
# "Example Plugin" and DESCRIPTION are.
cmake_minimum_required(VERSION 3.21)

project(Hello
    VERSION 0.1.0
    DESCRIPTION "The model of a TuxAPE plugin"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The engine goes into the plugin, a shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Qt, for the whole project: TuxAPE's, or an earlier Qt 6 (checked by
# tuxape_add_plugin).
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# TuxAPE's plugin SDK: plugin.h and TuxAPEPlugin.cmake, all the plugin
# needs of TuxAPE.
set(TUXAPE_SDK "${CMAKE_SOURCE_DIR}/../TuxAPE/sdk" CACHE PATH "TuxAPE's sdk folder")
if(NOT EXISTS "${TUXAPE_SDK}/plugin.h" OR NOT EXISTS "${TUXAPE_SDK}/TuxAPEPlugin.cmake")
    message(FATAL_ERROR "TuxAPE's SDK is not in ${TUXAPE_SDK}: set TUXAPE_SDK to TuxAPE's sdk folder.")
endif()
include("${TUXAPE_SDK}/TuxAPEPlugin.cmake")

option(HELLO_BUILD_TESTS "Build the test programs" ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# What the plugin does, without Qt: tested by itself.
add_subdirectory(src/engine)
# What TuxAPE loads: the windows, and the plugin's class.
add_subdirectory(src/plugin)

# The archive users install: dist/hello-<version>-<platform>.zip, with the
# plugin's translations built into its library; signed when the author's
# secret key is named (TUXAPE_SIGN_KEY).
tuxape_add_plugin(hello
    ID hello
    NAME "Hello – Example Plugin"
    VERSION ${PROJECT_VERSION}
    AUTHOR "Prénom Nom"
    LICENCE "MIT"
    DESCRIPTION "Says what the CPC's screen is, and types HELLO on its keyboard: the model of a TuxAPE plugin."
    API 4
    FILES LICENSE
    TRANSLATIONS translations/hello_fr.ts
    TRANSLATE_TARGETS hello_ui)

if(HELLO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
