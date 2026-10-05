add_library(tuxape_core STATIC
    asic.cpp
    audio.cpp
    autotype.cpp
    cartridge.cpp
    cpc.cpp
    crtc.cpp
    csl.cpp
    disasm.cpp
    disc.cpp
    fdc.cpp
    files.cpp
    gate_array.cpp
    inifile.cpp
    keymap.cpp
    memory.cpp
    monitor.cpp
    psg.cpp
    recording.cpp
    screen_text.cpp
    setup.cpp
    snapshot.cpp
    tape.cpp
    version.cpp
    zip.cpp
)

# ZIP archives are unpacked with zlib.
find_package(ZLIB REQUIRED)
target_link_libraries(tuxape_core PRIVATE ZLIB::ZLIB)

target_include_directories(tuxape_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tuxape_core PRIVATE
    TUXAPE_VERSION="${PROJECT_VERSION}"
    # Where the ROM images are looked for when TUXAPE_ROM_DIR is not set.
    TUXAPE_DEV_ROM_DIR="${PROJECT_SOURCE_DIR}/WinAPE20B2/ROM"
)
