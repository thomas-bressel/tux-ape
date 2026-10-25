find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(SDL2 REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Everything but main() is a library, so that tests can drive the windows.
add_library(tuxape_app STATIC
    asicdialog.cpp
    asicdialog.h
    assemblerdialog.cpp
    assemblerdialog.h
    audiooutput.cpp
    audiooutput.h
    avirecorder.cpp
    avirecorder.h
    breakpointsdialog.cpp
    breakpointsdialog.h
    contrast.cpp
    contrast.h
    debuggerdialog.cpp
    debuggerdialog.h
    disceditordialog.cpp
    disceditordialog.h
    discdialogs.cpp
    discdialogs.h
    discmanager.cpp
    discmanager.h
    drivesound.cpp
    drivesound.h
    emulator.cpp
    emulator.h
    filedialogs.cpp
    filedialogs.h
    graphicsdialog.cpp
    graphicsdialog.h
    help.cpp
    help.h
    hostjoystick.cpp
    hostjoystick.h
    hostkeys.cpp
    hostkeys.h
    icons.cpp
    icons.h
    language.cpp
    language.h
    librarydialog.cpp
    librarydialog.h
    mainwindow.cpp
    mainwindow.h
    ${PROJECT_SOURCE_DIR}/sdk/plugin.h
    pluginmanager.cpp
    pluginmanager.h
    registersdialog.cpp
    registersdialog.h
    crtview.cpp
    crtview.h
    screenwidget.cpp
    screenwidget.h
    settings.cpp
    settings.h
    setupdialog.cpp
    setupdialog.h
    tapedialog.cpp
    tapedialog.h
    theme.cpp
    theme.h
    tooldialogs.cpp
    tooldialogs.h
    tuxape.qrc
    welcome.cpp
    welcome.h
)
# The plugins' contract is the SDK's: what plugins are built against.
target_include_directories(tuxape_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/sdk)
target_compile_definitions(tuxape_app PRIVATE
    # The Library's own folder, when TUXAPE_LIBRARY_DIR is not set and there
    # is none beside the program.
    TUXAPE_DEV_LIBRARY_DIR="${PROJECT_SOURCE_DIR}/library"
    # The plugins folder, when TUXAPE_PLUGIN_DIR is not set and there is
    # none beside the program.
    TUXAPE_DEV_PLUGIN_DIR="${PROJECT_SOURCE_DIR}/plugins"
)
target_link_libraries(tuxape_app PUBLIC tuxape_core tuxape_sign Qt6::Widgets Qt6::OpenGLWidgets)
target_link_libraries(tuxape_app PRIVATE SDL2::SDL2)

# The languages of the interface: English, the language of the sources, and
# one translations/tuxape_<language>.ts for each other. A file dropped there
# is a language more, at the next configure; the Language menu finds it.
# English has a file too, tuxape_en.ts, of its plurals alone: the sources
# say "%n file(s)", and English "1 file", "3 files".
# `cmake --build build --target update_translations` brings the files up
# to date with the sources (lupdate).
# (qt_add_translations() does the same in one call, but breaks on a source
# folder whose path has a space in it.)
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
file(GLOB TUXAPE_TS_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translations/tuxape_*.ts)
set(TUXAPE_PLURALS_TS ${TUXAPE_TS_FILES})
list(FILTER TUXAPE_PLURALS_TS INCLUDE REGEX "/tuxape_en\\.ts$")
set(TUXAPE_LANGUAGES_TS ${TUXAPE_TS_FILES})
list(FILTER TUXAPE_LANGUAGES_TS EXCLUDE REGEX "/tuxape_en\\.ts$")
get_target_property(TUXAPE_APP_SOURCES tuxape_app SOURCES)
list(FILTER TUXAPE_APP_SOURCES INCLUDE REGEX "\\.(cpp|h)$")
qt_add_lupdate(
    TS_FILES ${TUXAPE_LANGUAGES_TS}
    SOURCES ${TUXAPE_APP_SOURCES} main.cpp
    LUPDATE_TARGET update_translations
    OPTIONS -no-obsolete -locations none)
if(TUXAPE_PLURALS_TS)
    qt_add_lupdate(
        TS_FILES ${TUXAPE_PLURALS_TS}
        SOURCES ${TUXAPE_APP_SOURCES} main.cpp
        LUPDATE_TARGET update_plurals
        OPTIONS -no-obsolete -locations none -pluralonly)
    add_dependencies(update_translations update_plurals)
endif()
qt_add_lrelease(
    TS_FILES ${TUXAPE_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE TUXAPE_QM_FILES)
qt_add_resources(tuxape_app tuxape_translations
    PREFIX /i18n
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${TUXAPE_QM_FILES})

add_executable(tuxape main.cpp)
target_link_libraries(tuxape PRIVATE tuxape_app)

# Each program built goes to dist/, TuxAPE ready to run, with the Qt
# libraries it needs and the folders beside it (see tools/update-dist.cmake).
# The target "dist" does it again on demand, the program unchanged: a dist/
# deleted is made again.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(update_dist ${CMAKE_COMMAND}
        -DPROGRAM=$<TARGET_FILE:tuxape>
        -DROOT=${PROJECT_SOURCE_DIR}
        -DQT_LIBS=$<TARGET_FILE_DIR:Qt6::Core>
        -DSTRIP=${CMAKE_STRIP}
        -P ${PROJECT_SOURCE_DIR}/tools/update-dist.cmake)
    if(TUXAPE_UPDATE_DIST)
        add_custom_command(TARGET tuxape POST_BUILD COMMAND ${update_dist} COMMENT "Bringing dist/ up to date" VERBATIM)
    endif()
    add_custom_target(dist COMMAND ${update_dist} DEPENDS tuxape COMMENT "Bringing dist/ up to date" VERBATIM)
endif()
