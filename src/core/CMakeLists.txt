add_library(tuxape_core STATIC
    version.cpp
)

target_include_directories(tuxape_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tuxape_core PRIVATE TUXAPE_VERSION="${PROJECT_VERSION}")
