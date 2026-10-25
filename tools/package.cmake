# Makes the folder of TuxAPE for one system, ready to be zipped and given:
# the program with all of Qt it needs, and the folders beside it, as dist/
# has them, but empty of what is not the project's to give (ROMs, the
# Library's programs). Run by the builds of .github/workflows/build.yml:
#
#   cmake -DPROGRAM=<the built tuxape, tuxape.exe or tuxape.app>
#         -DQT=<Qt's folder, with bin/ lib/ plugins/> -DROOT=<the sources>
#         -DOUT=<the folder to make> [-DSTRIP=strip] -P tools/package.cmake
#
#   Linux    tuxape, lib/ (Qt's libraries, SDL2's), plugins/ (Qt's), qt.conf
#   Windows  tuxape.exe and what windeployqt puts beside it, with the C++
#            runtime
#   Mac      TuxAPE.app, Qt inside it (macdeployqt), signed for itself
#
# and, beside the program on all of them: profile/ (TuxAPE's profiles),
# roms/, plugins/, library/, assembly/, dsk/, savestates/, and img/ with
# the cards' files blank.

cmake_minimum_required(VERSION 3.21)

foreach(variable PROGRAM QT ROOT OUT)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "package.cmake: ${variable} is not given")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE failed)
    if(failed)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "package.cmake: ${command} failed")
    endif()
endfunction()

# ---- The program and Qt ----
if(CMAKE_HOST_WIN32)
    file(COPY_FILE "${PROGRAM}" "${OUT}/tuxape.exe")
    run("${QT}/bin/windeployqt.exe" --release --no-system-d3d-compiler "${OUT}/tuxape.exe")
    # The C++ runtime, beside the program: nothing to install first.
    file(GLOB runtime "$ENV{VCToolsRedistDir}/$ENV{VSCMD_ARG_TGT_ARCH}/Microsoft.VC*.CRT/*.dll")
    if(NOT runtime)
        message(WARNING "package.cmake: the C++ runtime was not found; the user must install it")
    endif()
    foreach(library IN LISTS runtime)
        get_filename_component(name "${library}" NAME)
        file(COPY_FILE "${library}" "${OUT}/${name}")
    endforeach()
elseif(CMAKE_HOST_APPLE)
    file(COPY "${PROGRAM}/" DESTINATION "${OUT}/TuxAPE.app")
    run("${QT}/bin/macdeployqt" "${OUT}/TuxAPE.app")
    # Signed for itself: a Mac with Apple's processor runs no program that
    # is not signed at all.
    run(codesign --force --deep --sign - "${OUT}/TuxAPE.app")
else()
    file(COPY_FILE "${PROGRAM}" "${OUT}/tuxape")
    file(RPATH_SET FILE "${OUT}/tuxape" NEW_RPATH "\$ORIGIN/lib")
    if(STRIP)
        run("${STRIP}" "${OUT}/tuxape")
    endif()
    # Qt's plugins TuxAPE needs, in Qt's own layout: each finds Qt's
    # libraries two folders up, in lib/.
    foreach(kind platforms wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration
                 xcbglintegrations egldeviceintegrations imageformats iconengines platforminputcontexts generic)
        if(IS_DIRECTORY "${QT}/plugins/${kind}")
            file(COPY "${QT}/plugins/${kind}" DESTINATION "${OUT}/plugins")
        endif()
    endforeach()
    file(REMOVE "${OUT}/plugins/platforms/libqvnc.so" "${OUT}/plugins/platforms/libqlinuxfb.so"
                "${OUT}/plugins/platforms/libqvkkhrdisplay.so")
    file(WRITE "${OUT}/qt.conf" "[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\nTranslations = translations\n")
    file(GLOB translations "${QT}/translations/qtbase_*.qm")
    if(translations)
        file(COPY ${translations} DESTINATION "${OUT}/translations")
    endif()
    # The libraries the program and its plugins use: Qt's from Qt's
    # folder; SDL2 and xcb-cursor (which the X11 plugin needs and some
    # systems lack) from the system. The rest is every Linux's own.
    file(GLOB_RECURSE plugins "${OUT}/plugins/*.so")
    set(wanted "${QT}/lib/" "libSDL2-2.0.so" "libxcb-cursor.so")
    file(MAKE_DIRECTORY "${OUT}/lib")
    foreach(file "${OUT}/tuxape" ${plugins})
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${QT}/lib" ldd "${file}" OUTPUT_VARIABLE linked)
        string(REPLACE "\n" ";" linked "${linked}")
        foreach(line IN LISTS linked)
            if(NOT line MATCHES "=> (/[^ ]+) \\(0x")
                continue()
            endif()
            set(library "${CMAKE_MATCH_1}")
            set(take FALSE)
            foreach(kind IN LISTS wanted)
                string(FIND "${library}" "${kind}" at)
                if(NOT at EQUAL -1)
                    set(take TRUE)
                endif()
            endforeach()
            if(take)
                get_filename_component(name "${library}" NAME)
                file(REAL_PATH "${library}" real)
                file(COPY_FILE "${real}" "${OUT}/lib/${name}" ONLY_IF_DIFFERENT)
            endif()
        endforeach()
    endforeach()
endif()

# ---- The folders beside it ----
foreach(folder profile roms plugins library assembly dsk img savestates)
    file(MAKE_DIRECTORY "${OUT}/${folder}")
endforeach()
file(GLOB profiles "${ROOT}/profile/*.tpf")
if(profiles)
    file(COPY ${profiles} DESTINATION "${OUT}/profile")
endif()
if(EXISTS "${ROOT}/plugins/README.md")
    file(COPY "${ROOT}/plugins/README.md" DESTINATION "${OUT}/plugins")
endif()
# The cards' files, blank: an X-MASS disc of nothing written, an X-MEM
# flash all erased (&FF).
file(TOUCH "${OUT}/img/xmass.img")
string(ASCII 255 erased)
string(REPEAT "${erased}" 16384 rom)
string(REPEAT "${rom}" 32 flash)
file(WRITE "${OUT}/img/xmem.flash" "${flash}")
message(STATUS "TuxAPE is in ${OUT}")
