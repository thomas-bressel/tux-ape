find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(tuxape
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(tuxape PRIVATE tuxape_core Qt6::Widgets)
