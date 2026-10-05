add_library(tuxape_core STATIC
    autotype.cpp
    cpc.cpp
    crtc.cpp
    files.cpp
    gate_array.cpp
    keymap.cpp
    memory.cpp
    monitor.cpp
    psg.cpp
    screen_text.cpp
    setup.cpp
    version.cpp
)

target_include_directories(tuxape_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tuxape_core PRIVATE
    TUXAPE_VERSION="${PROJECT_VERSION}"
    # Where the ROM images are looked for when TUXAPE_ROM_DIR is not set.
    TUXAPE_DEV_ROM_DIR="${PROJECT_SOURCE_DIR}/WinAPE20B2/ROM"
)
