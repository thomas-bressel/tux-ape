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
# `cmake --build build --target update_translations` brings the files up
# to date with the sources (lupdate).
# (qt_add_translations() does the same in one call, but breaks on a source
# folder whose path has a space in it.)
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
file(GLOB TUXAPE_TS_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translations/tuxape_*.ts)
get_target_property(TUXAPE_APP_SOURCES tuxape_app SOURCES)
list(FILTER TUXAPE_APP_SOURCES INCLUDE REGEX "\\.(cpp|h)$")
qt_add_lupdate(
    TS_FILES ${TUXAPE_TS_FILES}
    SOURCES ${TUXAPE_APP_SOURCES} main.cpp
    LUPDATE_TARGET update_translations
    OPTIONS -no-obsolete -locations none)
qt_add_lrelease(
    TS_FILES ${TUXAPE_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE TUXAPE_QM_FILES)
qt_add_resources(tuxape_app tuxape_translations
    PREFIX /i18n
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${TUXAPE_QM_FILES})

add_executable(tuxape main.cpp)
target_link_libraries(tuxape PRIVATE tuxape_app)
