add_library(tuxape_core STATIC
    asic.cpp
    assembler.cpp
    audio.cpp
    autotype.cpp
    cartridge.cpp
    condition.cpp
    cpc.cpp
    crtc.cpp
    csl.cpp
    disasm.cpp
    disc.cpp
    discfiles.cpp
    fdc.cpp
    files.cpp
    gate_array.cpp
    ide.cpp
    inifile.cpp
    keymap.cpp
    keytables.cpp
    memory.cpp
    monitor.cpp
    psg.cpp
    recording.cpp
    screen_text.cpp
    session.cpp
    setup.cpp
    snapshot.cpp
    symbols.cpp
    tape.cpp
    version.cpp
    winape_session.cpp
    xmem.cpp
    zip.cpp
)

# ZIP archives are unpacked with zlib.
find_package(ZLIB REQUIRED)
target_link_libraries(tuxape_core PRIVATE ZLIB::ZLIB)

target_include_directories(tuxape_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(tuxape_core PRIVATE
    TUXAPE_VERSION="${PROJECT_VERSION}"
    # Where the ROM images and the profiles are looked for when the
    # variables do not say: the project's roms/ (the user's, the machines'
    # own in roms/system/) and profile/, TuxAPE's own.
    TUXAPE_DEV_ROM_DIR="${PROJECT_SOURCE_DIR}/roms"
    TUXAPE_DEV_USER_ROM_DIR="${PROJECT_SOURCE_DIR}/roms"
    TUXAPE_DEV_PROFILE_DIR="${PROJECT_SOURCE_DIR}/profile"
)
