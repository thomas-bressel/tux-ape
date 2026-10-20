find_package(Qt6 REQUIRED COMPONENTS Widgets)

# The engine alone.
add_executable(engine_greeting engine_greeting.cpp)
target_link_libraries(engine_greeting PRIVATE hello_engine)
add_test(NAME engine_greeting COMMAND engine_greeting)

# The plugin as TuxAPE loads it, with a host of the test's own.
add_executable(plugin_window plugin_window.cpp)
target_include_directories(plugin_window PRIVATE ${TUXAPE_SDK})
target_link_libraries(plugin_window PRIVATE Qt6::Widgets)
add_dependencies(plugin_window hello)
target_compile_definitions(plugin_window PRIVATE HELLO_PLUGIN="$<TARGET_FILE:hello>")
add_test(NAME plugin_window COMMAND plugin_window)
set_tests_properties(plugin_window PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
