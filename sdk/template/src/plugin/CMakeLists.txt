set(CMAKE_AUTOMOC ON)

# The windows are a library, so that tests can drive them without TuxAPE.
add_library(hello_ui STATIC
    hellowindow.cpp
    hellowindow.h
)
target_include_directories(hello_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${TUXAPE_SDK})
target_link_libraries(hello_ui PUBLIC hello_engine Qt6::Widgets)

# What TuxAPE loads: the plugin's class, and no more. tuxape_add_plugin()
# names it after the plugin's id.
add_library(hello MODULE
    helloplugin.cpp
    helloplugin.h
)
target_link_libraries(hello PRIVATE hello_ui)
