# TuxAPEPlugin.cmake: what a plugin for TuxAPE is built with, so that it is
# built the way TuxAPE expects (see PLUGINS.md at the root of TuxAPE).
#
#   include(/path/to/TuxAPE/sdk/TuxAPEPlugin.cmake)
#   add_library(monplugin MODULE ...)
#   tuxape_add_plugin(monplugin
#       ID monplugin  NAME "Mon Plugin – Sprite Editor"  VERSION ${PROJECT_VERSION}
#       AUTHOR "Prénom Nom"  LICENCE "MIT"  API 4
#       DESCRIPTION "Ce qu'il fait."
#       FILES LICENSE  PROGRAMS monplugin_cli
#       TRANSLATIONS translations/monplugin_fr.ts  TRANSLATE_TARGETS monplugin_ui)
#
# It checks what TuxAPE would otherwise refuse when it loads the plugin:
# the id, the version of the plugin interface, the Qt the plugin is built
# with. It names the library after the id, builds the plugin's translations
# into it, writes plugin.json, puts the plugin's folder together, signs it
# when the author's secret key is named (TUXAPE_SIGN_KEY, TUXAPE_SIGN_TOOL),
# and packs it into dist/<id>-<version>-<platform>.zip.
#
# TRANSLATIONS names the plugin's .ts files (Qt Linguist), one a language:
# <id>_<language>.ts. Each is built into the library as
# :/i18n/<id>/<id>_<language>.qm, for the plugin to load in start(), in the
# language PluginHost::language() says. TRANSLATE_TARGETS are the targets,
# besides the module, whose sources hold texts to translate (the windows);
# `cmake --build build --target <id>_update_translations` brings the .ts
# files up to date with them (lupdate).

include_guard(GLOBAL)

set(TUXAPE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The versions of the plugin interface this SDK's TuxAPE loads, read from
# plugin.h itself.
file(STRINGS "${TUXAPE_SDK_DIR}/plugin.h" TUXAPE_API_LINES REGEX "constexpr int k(Oldest)?PluginApi = [0-9]+;")
foreach(line ${TUXAPE_API_LINES})
    string(REGEX MATCH "[0-9]+" value "${line}")
    if(line MATCHES "kOldestPluginApi")
        set(TUXAPE_OLDEST_PLUGIN_API ${value})
    else()
        set(TUXAPE_PLUGIN_API ${value})
    endif()
endforeach()

# The Qt TuxAPE is built with. Qt does not load a plugin built with a later
# Qt 6 than the one it runs with: a plugin is built with this one, or an
# earlier Qt 6.
set(TUXAPE_QT_VERSION "6.10")

# What the computer is called in a plugin.json, as TuxAPE names the one it
# runs on: linux-x86_64, linux-arm64, windows-x86_64, macos-arm64...
function(tuxape_platform out)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" system)
    if(system STREQUAL "darwin")
        set(system "macos")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" processor)
    if(processor STREQUAL "amd64")
        set(processor "x86_64")
    elseif(processor STREQUAL "aarch64")
        set(processor "arm64")
    endif()
    set(${out} "${system}-${processor}" PARENT_SCOPE)
endfunction()

# The sources of targets, as full paths: what lupdate reads.
function(tuxape_target_sources out)
    set(files "")
    foreach(target ${ARGN})
        get_target_property(sources ${target} SOURCES)
        get_target_property(folder ${target} SOURCE_DIR)
        foreach(source ${sources})
            if(source MATCHES "\\.(cpp|h)$")
                cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${folder}" OUTPUT_VARIABLE source)
                list(APPEND files "${source}")
            endif()
        endforeach()
    endforeach()
    set(${out} ${files} PARENT_SCOPE)
endfunction()

# The plugin's .ts files, compiled and built into the library under
# :/i18n/<id>/, and the <id>_update_translations target that brings them up
# to date. (qt_add_translations() does both, but breaks on a source folder
# whose path has a space in it.)
function(tuxape_add_plugin_translations target id translations translate_targets)
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
    set(ts_files "")
    foreach(file ${translations})
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE file)
        if(NOT file MATCHES "/${id}_[a-zA-Z_]+\\.ts$")
            message(FATAL_ERROR "A translation of ${id} is named ${id}_<language>.ts: ${file} is not.")
        endif()
        list(APPEND ts_files "${file}")
    endforeach()
    tuxape_target_sources(sources ${target} ${translate_targets})
    qt_add_lupdate(
        TS_FILES ${ts_files}
        SOURCES ${sources}
        LUPDATE_TARGET ${id}_update_translations
        OPTIONS -no-obsolete -locations none)
    qt_add_lrelease(
        TS_FILES ${ts_files}
        QM_FILES_OUTPUT_VARIABLE qm_files)
    qt_add_resources(${target} ${id}_translations
        PREFIX "/i18n/${id}"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${qm_files})
endfunction()

function(tuxape_add_plugin target)
    cmake_parse_arguments(P "" "ID;NAME;VERSION;AUTHOR;LICENCE;DESCRIPTION;API" "FILES;PROGRAMS;TRANSLATIONS;TRANSLATE_TARGETS" ${ARGN})

    # ---- What TuxAPE would refuse ----
    if(NOT P_ID MATCHES "^[a-z0-9][a-z0-9_-]*$" OR P_ID MATCHES "^.................................")
        message(FATAL_ERROR "The plugin's id \"${P_ID}\" is not one: small letters, digits, '-' and '_', 32 at most, "
                            "the first a letter or a digit.")
    endif()
    foreach(field NAME VERSION AUTHOR LICENCE DESCRIPTION API)
        if("${P_${field}}" STREQUAL "")
            message(FATAL_ERROR "tuxape_add_plugin(${target}): ${field} is missing.")
        endif()
    endforeach()
    if(P_API LESS TUXAPE_OLDEST_PLUGIN_API OR P_API GREATER TUXAPE_PLUGIN_API)
        message(FATAL_ERROR "The plugin asks for version ${P_API} of the plugin interface; this SDK's TuxAPE loads "
                            "versions ${TUXAPE_OLDEST_PLUGIN_API} to ${TUXAPE_PLUGIN_API}.")
    endif()
    get_target_property(type ${target} TYPE)
    if(NOT type STREQUAL "MODULE_LIBRARY")
        message(FATAL_ERROR "${target} is to be made with add_library(${target} MODULE ...): TuxAPE loads it.")
    endif()
    # Asked here, whatever folder the plugin found Qt from: the version is
    # then known in this function.
    find_package(Qt6 REQUIRED COMPONENTS Core)
    if(NOT Qt6Core_VERSION)
        message(FATAL_ERROR "The version of Qt the plugin is built with cannot be told.")
    endif()
    if(Qt6Core_VERSION VERSION_GREATER_EQUAL "${TUXAPE_QT_VERSION}.9999")
        message(FATAL_ERROR "The plugin is built with Qt ${Qt6Core_VERSION}, later than TuxAPE's Qt ${TUXAPE_QT_VERSION}: "
                            "TuxAPE would not load it. Build it with Qt ${TUXAPE_QT_VERSION} or an earlier Qt 6.")
    endif()
    if(CMAKE_BUILD_TYPE MATCHES "^Debug$")
        message(WARNING "A plugin given to others is built as Release: Qt may not load a Debug one into a Release TuxAPE.")
    endif()

    # ---- The library: named after the id, against the SDK alone ----
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${P_ID}" AUTOMOC ON)
    target_include_directories(${target} PRIVATE "${TUXAPE_SDK_DIR}")

    # ---- Its translations, built into it ----
    if(P_TRANSLATIONS)
        tuxape_add_plugin_translations(${target} ${P_ID} "${P_TRANSLATIONS}" "${P_TRANSLATE_TARGETS}")
    endif()

    # ---- Its folder: plugin.json, the library, the programs, the files ----
    tuxape_platform(platform)
    set(stage "${CMAKE_BINARY_DIR}/stage/${P_ID}")
    set(TUXAPE_DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Where the plugin's archive is made")
    set(archive "${TUXAPE_DIST_DIR}/${P_ID}-${P_VERSION}-${platform}.zip")
    # The texts as JSON has them: their backslashes and quotes escaped.
    foreach(field NAME VERSION AUTHOR LICENCE DESCRIPTION)
        string(REPLACE "\\" "\\\\" text "${P_${field}}")
        string(REPLACE "\"" "\\\"" json_${field} "${text}")
    endforeach()
    set(manifest "${CMAKE_BINARY_DIR}/${P_ID}-plugin.json")
    file(GENERATE OUTPUT "${manifest}" CONTENT "{
    \"id\": \"${P_ID}\",
    \"name\": \"${json_NAME}\",
    \"version\": \"${json_VERSION}\",
    \"api\": ${P_API},
    \"author\": \"${json_AUTHOR}\",
    \"licence\": \"${json_LICENCE}\",
    \"description\": \"${json_DESCRIPTION}\",
    \"libraries\": {
        \"${platform}\": \"${platform}/$<TARGET_FILE_NAME:${target}>\"
    }
}
")
    set(copy_programs "")
    foreach(program ${P_PROGRAMS})
        list(APPEND copy_programs COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}/bin"
             COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${program}>" "${stage}/bin/")
    endforeach()
    set(copy_files "")
    foreach(file ${P_FILES})
        list(APPEND copy_files COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/${file}" "${stage}/")
    endforeach()

    # ---- Signed with the author's secret key, when one is named ----
    # Named on purpose only (-DTUXAPE_SIGN_KEY=..., or the environment's
    # TUXAPE_SIGN_KEY): whoever builds someone else's plugin is not to sign
    # it in their own name without meaning to.
    set(TUXAPE_SIGN_KEY "$ENV{TUXAPE_SIGN_KEY}" CACHE FILEPATH "The secret key plugins are signed with")
    set(TUXAPE_SIGN_TOOL "${TUXAPE_SDK_DIR}/../build/src/sign/tuxape-plugin-sign" CACHE FILEPATH "TuxAPE's tuxape-plugin-sign")
    if(EXISTS "${TUXAPE_SIGN_KEY}" AND EXISTS "${TUXAPE_SIGN_TOOL}")
        set(sign COMMAND "${TUXAPE_SIGN_TOOL}" sign "${stage}" "${TUXAPE_SIGN_KEY}")
    else()
        message(STATUS "${P_ID}: the archive is not signed (TUXAPE_SIGN_KEY names no secret key, or TUXAPE_SIGN_TOOL no tool)")
        set(sign COMMAND "${CMAKE_COMMAND}" -E rm -f "${stage}/signature.json")
    endif()

    # The folder made afresh each time: a file left from before would go
    # into the archive.
    add_custom_target(${P_ID}_zip ALL
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${stage}/${platform}" "${TUXAPE_DIST_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${manifest}" "${stage}/plugin.json"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${target}>" "${stage}/${platform}/"
        ${copy_programs}
        ${copy_files}
        ${sign}
        COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_BINARY_DIR}/stage" "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip "${P_ID}"
        DEPENDS ${target} ${P_PROGRAMS}
        COMMENT "Packing ${archive}"
        VERBATIM)
endfunction()
