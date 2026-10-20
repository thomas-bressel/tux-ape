# What the plugin does, without Qt nor TuxAPE: plain C++, tested alone.
add_library(hello_engine STATIC
    greeting.cpp
    greeting.h
)
target_include_directories(hello_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
