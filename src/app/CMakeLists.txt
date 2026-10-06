find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
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
    debuggerdialog.cpp
    debuggerdialog.h
    disceditordialog.cpp
    disceditordialog.h
    discdialogs.cpp
    discdialogs.h
    discmanager.cpp
    discmanager.h
    emulator.cpp
    emulator.h
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
    screenwidget.cpp
    screenwidget.h
    settings.cpp
    settings.h
    setupdialog.cpp
    setupdialog.h
    tapedialog.cpp
    tapedialog.h
    tooldialogs.cpp
    tooldialogs.h
)
target_include_directories(tuxape_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tuxape_app PUBLIC tuxape_core Qt6::Widgets)
target_link_libraries(tuxape_app PRIVATE SDL2::SDL2)

add_executable(tuxape main.cpp)
target_link_libraries(tuxape PRIVATE tuxape_app)
