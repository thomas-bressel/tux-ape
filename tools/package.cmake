# Makes the folder of TuxAPE for Linux, ready to be packed and given:
# the program with all of Qt it needs, and the folders beside it, as dist/
# has them: the ROMs of the sources' roms/, their library/ with its folders
# and programs. Sources without them (GitHub's builds) leave roms/ and
# library/ empty. Run by
# tools/linux-release.sh and by the builds of .github/workflows/build.yml:
#
#   cmake -DPROGRAM=<the built tuxape>
#         -DQT=<Qt's folder, with bin/ lib/ plugins/> -DROOT=<the sources>
#         -DOUT=<the folder to make> [-DSTRIP=strip] -P tools/package.cmake
#
#   tuxape, and qt/ with Qt in its own layout: lib/, plugins/,
#   translations/; qt.conf beside the program says so
#
# and, beside the program: LISEZMOI.txt and README.txt, profile/ (TuxAPE's
# profiles), roms/, plugins/, library/, assembly/, dsk/, savestates/, and
# img/ with the cards' files blank.

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
file(COPY_FILE "${PROGRAM}" "${OUT}/tuxape")
file(RPATH_SET FILE "${OUT}/tuxape" NEW_RPATH "\$ORIGIN/qt/lib")
if(STRIP)
    run("${STRIP}" "${OUT}/tuxape")
endif()
# Qt's plugins TuxAPE needs, in Qt's own layout: each finds Qt's
# libraries two folders up, in qt/lib/. Not in plugins/: that folder is
# for TuxAPE's own.
# The windows on X11 or Wayland, or on none (offscreen); not straight on
# the screen without a desktop (eglfs, linuxfb, vkkhrdisplay), nor through
# VNC.
foreach(kind platforms wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration
             xcbglintegrations imageformats iconengines platforminputcontexts generic)
    if(IS_DIRECTORY "${QT}/plugins/${kind}")
        file(COPY "${QT}/plugins/${kind}" DESTINATION "${OUT}/qt/plugins")
    endif()
endforeach()
foreach(platform vnc linuxfb vkkhrdisplay eglfs minimal minimalegl)
    file(REMOVE "${OUT}/qt/plugins/platforms/libq${platform}.so")
endforeach()
file(WRITE "${OUT}/qt.conf" "[Paths]\nPrefix = qt\nLibraries = lib\nPlugins = plugins\nTranslations = translations\n")
file(GLOB translations "${QT}/translations/qtbase_*.qm")
if(translations)
    file(COPY ${translations} DESTINATION "${OUT}/qt/translations")
endif()
# The libraries the program and its plugins use: Qt's from Qt's
# folder; from the system, what the X11 plugin needs that only Qt's
# programs do (the xcb utilities, the shape extension, the X11 keyboard of
# xkbcommon), which a desktop without them lacks. xkbcommon goes with its
# X11 part: one of 2022 beside a system's newer xkbcommon crashes.
# SDL2 is in the program (tools/build-sdl2.sh). The rest every desktop
# has (GTK's needs).
file(GLOB_RECURSE plugins "${OUT}/qt/plugins/*.so")
set(wanted "${QT}/lib/" "libxcb-cursor.so" "libxcb-icccm.so" "libxcb-image.so" "libxcb-keysyms.so"
           "libxcb-render-util.so" "libxcb-util.so" "libxcb-shape.so" "libxcb-xkb.so" "libxkbcommon-x11.so"
           "libxkbcommon.so")
file(MAKE_DIRECTORY "${OUT}/qt/lib")
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
            file(COPY_FILE "${real}" "${OUT}/qt/lib/${name}" ONLY_IF_DIFFERENT)
        endif()
    endforeach()
endforeach()

# Qt's licence, the LGPL version 3, and the GPL it is written against.
foreach(licence LGPL-3 GPL-3)
    if(EXISTS "/usr/share/common-licenses/${licence}")
        file(COPY "/usr/share/common-licenses/${licence}" DESTINATION "${OUT}/qt/LICENSES")
    endif()
endforeach()

# ---- The folders beside it ----
foreach(folder profile roms plugins library assembly dsk img savestates)
    file(MAKE_DIRECTORY "${OUT}/${folder}")
endforeach()
foreach(readme LISEZMOI.txt README.txt LICENCE.txt)
    if(EXISTS "${ROOT}/tools/linux-archive/${readme}")
        file(COPY "${ROOT}/tools/linux-archive/${readme}" DESTINATION "${OUT}")
    endif()
endforeach()
if(IS_DIRECTORY "${ROOT}/roms")
    file(COPY "${ROOT}/roms/" DESTINATION "${OUT}/roms")
endif()
if(IS_DIRECTORY "${ROOT}/library")
    file(GLOB_RECURSE folders LIST_DIRECTORIES true RELATIVE "${ROOT}/library" "${ROOT}/library/*")
    foreach(folder IN LISTS folders)
        if(IS_DIRECTORY "${ROOT}/library/${folder}")
            file(MAKE_DIRECTORY "${OUT}/library/${folder}")
        endif()
    endforeach()
    file(GLOB_RECURSE programs LIST_DIRECTORIES false RELATIVE "${ROOT}/library" "${ROOT}/library/*")
    foreach(program IN LISTS programs)
        get_filename_component(where "${OUT}/library/${program}" DIRECTORY)
        file(MAKE_DIRECTORY "${where}")
        file(COPY_FILE "${ROOT}/library/${program}" "${OUT}/library/${program}")
    endforeach()
endif()
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
