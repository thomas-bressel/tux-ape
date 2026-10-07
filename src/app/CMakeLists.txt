find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(SDL2 REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Everything but main() is a library, so that tests can drive the windows.
add_library(tuxape_app STATIC
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
    librarydialog.cpp
    librarydialog.h
    mainwindow.cpp
    mainwindow.h
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
target_include_directories(tuxape_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tuxape_app PRIVATE
    # The Library's own folder, when TUXAPE_LIBRARY_DIR is not set and there
    # is none beside the program.
    TUXAPE_DEV_LIBRARY_DIR="${PROJECT_SOURCE_DIR}/library"
)
target_link_libraries(tuxape_app PUBLIC tuxape_core Qt6::Widgets Qt6::OpenGLWidgets)
target_link_libraries(tuxape_app PRIVATE SDL2::SDL2)

add_executable(tuxape main.cpp)
target_link_libraries(tuxape PRIVATE tuxape_app)
