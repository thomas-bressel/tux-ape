find_package(Qt6 6.4 REQUIRED COMPONENTS Core)

# The plugins' signatures (see pluginsignature.h). tweetnacl.c and
# tweetnacl.h are TweetNaCl as Bernstein, van Gastel, Janssen, Lange,
# Schwabe and Smetsers published it (tweetnacl.cr.yp.to, 20140427), in the
# public domain, left as they are.
add_library(tuxape_sign STATIC
    pluginsignature.cpp
    pluginsignature.h
    tweetnacl.c
    tweetnacl.h
)
set_source_files_properties(tweetnacl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
target_include_directories(tuxape_sign PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tuxape_sign PUBLIC tuxape_core Qt6::Core)

# What plugins' authors sign them with.
add_executable(tuxape-plugin-sign signtool.cpp)
target_link_libraries(tuxape-plugin-sign PRIVATE tuxape_sign)
