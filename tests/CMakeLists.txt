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

add_executable(cpc_boot cpc_boot.cpp)
target_link_libraries(cpc_boot PRIVATE tuxape_core)
add_test(NAME cpc_boot COMMAND cpc_boot)
set_tests_properties(cpc_boot PROPERTIES SKIP_RETURN_CODE 77)

add_executable(disc_amsdos disc_amsdos.cpp)
target_link_libraries(disc_amsdos PRIVATE tuxape_core)
add_test(NAME disc_amsdos COMMAND disc_amsdos)
set_tests_properties(disc_amsdos PROPERTIES SKIP_RETURN_CODE 77)

add_executable(fdc_unit fdc_unit.cpp)
target_link_libraries(fdc_unit PRIVATE tuxape_core)
add_test(NAME fdc_unit COMMAND fdc_unit)

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
    add_executable(gui_smoke gui_smoke.cpp)
    target_link_libraries(gui_smoke PRIVATE tuxape_app Qt6::Test)
    add_test(NAME gui_smoke COMMAND gui_smoke)
    set_tests_properties(gui_smoke PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
