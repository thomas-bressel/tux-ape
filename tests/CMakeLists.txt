set(TUXAPE_TEST_ROMS ${CMAKE_CURRENT_SOURCE_DIR}/roms)

add_executable(z80_cpm z80_cpm.cpp)
target_link_libraries(z80_cpm PRIVATE tuxape_core)

# The exercisers are downloaded by tests/fetch-test-roms.sh; tests whose
# program is missing are simply not registered.
foreach(program prelim zexdoc zexall)
    if(EXISTS ${TUXAPE_TEST_ROMS}/${program}.com)
        add_test(NAME z80_${program} COMMAND z80_cpm ${TUXAPE_TEST_ROMS}/${program}.com)
        set_tests_properties(z80_${program} PROPERTIES TIMEOUT 600)
    endif()
endforeach()

add_executable(z80_interrupts z80_interrupts.cpp)
target_link_libraries(z80_interrupts PRIVATE tuxape_core)
add_test(NAME z80_interrupts COMMAND z80_interrupts)

add_executable(cpc_timing cpc_timing.cpp)
target_link_libraries(cpc_timing PRIVATE tuxape_core)
add_test(NAME cpc_timing COMMAND cpc_timing)

# Hardware test programs checked on real machines (tests/fetch-test-roms.sh
# --acid). The CRTC one needs over an hour of emulated time, so it runs as a
# separate test labelled "slow": skip it with  ctest -LE slow
add_executable(acid_tests acid_tests.cpp)
target_link_libraries(acid_tests PRIVATE tuxape_core)
add_test(NAME acid COMMAND acid_tests ${TUXAPE_TEST_ROMS}/acid psg cpu inout itest cpctest ppi)
add_test(NAME acid_crtc COMMAND acid_tests ${TUXAPE_TEST_ROMS}/acid crtctest)
add_test(NAME acid_plus COMMAND acid_tests ${TUXAPE_TEST_ROMS}/acid asiclock asicrom asicppi dmatest)
add_test(NAME acid_asic COMMAND acid_tests ${TUXAPE_TEST_ROMS}/acid asictest)
set_tests_properties(acid acid_crtc acid_plus acid_asic PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800)
set_tests_properties(acid_crtc acid_asic PROPERTIES LABELS slow)

add_executable(csl csl.cpp)
target_link_libraries(csl PRIVATE tuxape_core)
add_test(NAME csl COMMAND csl)

add_executable(video_timing video_timing.cpp)
target_link_libraries(video_timing PRIVATE tuxape_core)
add_test(NAME video_timing COMMAND video_timing)

add_executable(crtc0 crtc0.cpp)
target_link_libraries(crtc0 PRIVATE tuxape_core)
add_test(NAME crtc0 COMMAND crtc0)

add_executable(crtc_interlace crtc_interlace.cpp)
target_link_libraries(crtc_interlace PRIVATE tuxape_core)
add_test(NAME crtc_interlace COMMAND crtc_interlace)

add_executable(crtc1 crtc1.cpp)
target_link_libraries(crtc1 PRIVATE tuxape_core)
add_test(NAME crtc1 COMMAND crtc1)

add_executable(snapshot snapshot.cpp)
target_link_libraries(snapshot PRIVATE tuxape_core)
add_test(NAME snapshot COMMAND snapshot ${CMAKE_CURRENT_SOURCE_DIR}/roms/acid/snapshot)

add_executable(cpc_boot cpc_boot.cpp)
target_link_libraries(cpc_boot PRIVATE tuxape_core)
add_test(NAME cpc_boot COMMAND cpc_boot)
set_tests_properties(cpc_boot PROPERTIES SKIP_RETURN_CODE 77)

add_executable(core_speed core_speed.cpp)
target_link_libraries(core_speed PRIVATE tuxape_core)
add_test(NAME core_speed COMMAND core_speed)
set_tests_properties(core_speed PROPERTIES SKIP_RETURN_CODE 77)

add_executable(machine_setup machine_setup.cpp)
target_link_libraries(machine_setup PRIVATE tuxape_core)
add_test(NAME machine_setup COMMAND machine_setup)
set_tests_properties(machine_setup PROPERTIES SKIP_RETURN_CODE 77)

add_executable(disc_amsdos disc_amsdos.cpp)
target_link_libraries(disc_amsdos PRIVATE tuxape_core)
add_test(NAME disc_amsdos COMMAND disc_amsdos)
set_tests_properties(disc_amsdos PROPERTIES SKIP_RETURN_CODE 77)

add_executable(zip zip.cpp)
target_link_libraries(zip PRIVATE tuxape_core)
add_test(NAME zip COMMAND zip)

add_executable(session session.cpp)
target_link_libraries(session PRIVATE tuxape_core)
add_test(NAME session COMMAND session)
set_tests_properties(session PROPERTIES SKIP_RETURN_CODE 77)

add_executable(recording recording.cpp)
target_link_libraries(recording PRIVATE tuxape_core)
add_test(NAME recording COMMAND recording)

add_executable(assembler assembler.cpp)
target_link_libraries(assembler PRIVATE tuxape_core)
add_test(NAME assembler COMMAND assembler)

add_executable(condition condition.cpp)
target_link_libraries(condition PRIVATE tuxape_core)
add_test(NAME condition COMMAND condition)

add_executable(disasm disasm.cpp)
target_link_libraries(disasm PRIVATE tuxape_core)
add_test(NAME disasm COMMAND disasm)

add_executable(plus plus.cpp)
target_link_libraries(plus PRIVATE tuxape_core)
add_test(NAME plus COMMAND plus)

add_executable(tape tape.cpp)
target_link_libraries(tape PRIVATE tuxape_core)
add_test(NAME tape COMMAND tape)
set_tests_properties(tape PROPERTIES SKIP_RETURN_CODE 77)

add_executable(discfiles discfiles.cpp)
target_link_libraries(discfiles PRIVATE tuxape_core)
add_test(NAME discfiles COMMAND discfiles)

add_executable(fdc_unit fdc_unit.cpp)
target_link_libraries(fdc_unit PRIVATE tuxape_core)
add_test(NAME fdc_unit COMMAND fdc_unit)

add_executable(psg_sound psg_sound.cpp)
target_link_libraries(psg_sound PRIVATE tuxape_core)
add_test(NAME psg_sound COMMAND psg_sound)

add_executable(inifile inifile.cpp)
target_link_libraries(inifile PRIVATE tuxape_core)
target_compile_definitions(inifile PRIVATE TUXAPE_WINAPE_DIR="${PROJECT_SOURCE_DIR}/WinAPE20B2")
add_test(NAME inifile COMMAND inifile)

add_executable(keymap keymap.cpp)
target_link_libraries(keymap PRIVATE tuxape_core)
target_compile_definitions(keymap PRIVATE TUXAPE_WINAPE_DIR="${PROJECT_SOURCE_DIR}/WinAPE20B2")
add_test(NAME keymap COMMAND keymap)

add_executable(z80_sst z80_sst.cpp)
target_link_libraries(z80_sst PRIVATE tuxape_core)
if(EXISTS ${TUXAPE_TEST_ROMS}/sst)
    add_test(NAME z80_single_step COMMAND z80_sst ${TUXAPE_TEST_ROMS}/sst)
    set_tests_properties(z80_single_step PROPERTIES TIMEOUT 1200)
endif()

if(TUXAPE_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(test gui_smoke gui_discs gui_sound gui_snapshot gui_setup gui_tools gui_library gui_tape gui_plus gui_debugger gui_session gui_disceditor gui_assembler gui_registers gui_breakpoints gui_memtools)
        add_executable(${test} ${test}.cpp)
        target_link_libraries(${test} PRIVATE tuxape_app Qt6::Test)
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES
            SKIP_RETURN_CODE 77
            ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    endforeach()
    target_compile_definitions(gui_setup PRIVATE TUXAPE_WINAPE_DIR="${PROJECT_SOURCE_DIR}/WinAPE20B2")
endif()
