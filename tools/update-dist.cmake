# Brings dist/ up to date: TuxAPE ready to run, with its folders beside it.
# Run after each build of the program (src/app/CMakeLists.txt), or by hand:
#
#   cmake -DPROGRAM=build/src/app/tuxape -DROOT=. -DQT_LIBS=<Qt's lib folder>
#         [-DSTRIP=strip] [-DDIST=dist] -P tools/update-dist.cmake
#
# The program and the libraries it takes from the project's own Qt (.deps/)
# are brought each time. The rest is only made when it is not there, and
# nothing is deleted: what TuxAPE or the user put in dist/ stays.
#
#   dist/tuxape        the program, finding its libraries in lib/ beside it
#   dist/lib/          the Qt libraries of .deps/; the system's stay there
#   dist/roms/         every ROM image, the project's roms/: the machines'
#                      own in system/, the others in folders of their kind
#   dist/profile/      TuxAPE's profiles (.tpf), the project's profile/
#   dist/plugins/      no plugin, the folder and what it says of them
#   dist/library/      the Library window's folders, without their programs
#   dist/assembly/     assembler sources
#   dist/dsk/          disc images: those of the project's root that start
#                      the cards' software (see kDiscs)
#   dist/img/          what the cards keep, blank: an X-MASS disc of no
#                      sector written yet, and an X-MEM flash all erased
#   dist/savestates/   the states F5 keeps and F6 takes back

cmake_minimum_required(VERSION 3.21)

foreach(variable PROGRAM ROOT QT_LIBS)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "update-dist.cmake: ${variable} is not given")
    endif()
endforeach()
if(NOT DEFINED DIST)
    set(DIST "${ROOT}/dist")
endif()

# The discs of the project's root that dist/dsk/ starts with.
set(kDiscs iMPsuite_230807.hfe orgams230517.hfe xmass_xmem-install.hfe)

foreach(folder lib roms profile plugins library assembly dsk img savestates)
    file(MAKE_DIRECTORY "${DIST}/${folder}")
endforeach()

# ---- The program ----
# Copied beside its place and moved there: a TuxAPE running from dist/
# keeps the one it started with. It looks for its libraries in lib/ beside
# it, not in the project's .deps/: the folder may go to another machine.
set(new "${DIST}/.tuxape.new")
file(COPY_FILE "${PROGRAM}" "${new}")
file(RPATH_SET FILE "${new}" NEW_RPATH "\$ORIGIN/lib")
if(STRIP)
    execute_process(COMMAND "${STRIP}" "${new}" RESULT_VARIABLE stripped)
    if(NOT stripped EQUAL 0)
        message(WARNING "update-dist.cmake: ${STRIP} could not strip the program")
    endif()
endif()
file(RENAME "${new}" "${DIST}/tuxape")

# ---- Its libraries ----
# Those the program finds in the Qt it was built with, when that Qt is the
# project's own: a system's Qt is the system's to give.
cmake_path(IS_PREFIX ROOT "${QT_LIBS}" NORMALIZE ownQt)
if(ownQt)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "LD_LIBRARY_PATH=${QT_LIBS}" ldd "${PROGRAM}"
                    OUTPUT_VARIABLE linked RESULT_VARIABLE lddFailed)
    if(lddFailed)
        message(WARNING "update-dist.cmake: ldd could not list the program's libraries")
    endif()
    string(REPLACE "\n" ";" linked "${linked}")
    foreach(line IN LISTS linked)
        if(line MATCHES "=> (.+) \\(0x")
            set(library "${CMAKE_MATCH_1}")
            cmake_path(IS_PREFIX QT_LIBS "${library}" NORMALIZE inQt)
        else()
            set(inQt FALSE)
        endif()
        if(inQt)
            get_filename_component(name "${library}" NAME)
            # The library itself, under the name the program asks for.
            file(REAL_PATH "${library}" real)
            file(COPY_FILE "${real}" "${DIST}/lib/${name}" ONLY_IF_DIFFERENT)
        endif()
    endforeach()
endif()

# ---- The rest, when it is not there ----
# Each file of FROM (recursively) that TO does not have.
function(add_missing from to)
    if(NOT IS_DIRECTORY "${from}")
        return()
    endif()
    file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${from}" "${from}/*")
    foreach(file IN LISTS files)
        if(NOT EXISTS "${to}/${file}")
            get_filename_component(folder "${to}/${file}" DIRECTORY)
            file(MAKE_DIRECTORY "${folder}")
            file(COPY_FILE "${from}/${file}" "${to}/${file}")
        endif()
    endforeach()
endfunction()

add_missing("${ROOT}/roms" "${DIST}/roms")
add_missing("${ROOT}/profile" "${DIST}/profile")
if(EXISTS "${ROOT}/plugins/README.md" AND NOT EXISTS "${DIST}/plugins/README.md")
    file(COPY_FILE "${ROOT}/plugins/README.md" "${DIST}/plugins/README.md")
endif()

# The Library's folders: its categories, for the programs to go into.
if(IS_DIRECTORY "${ROOT}/library")
    file(GLOB_RECURSE folders LIST_DIRECTORIES true RELATIVE "${ROOT}/library" "${ROOT}/library/*")
    foreach(folder IN LISTS folders)
        if(IS_DIRECTORY "${ROOT}/library/${folder}")
            file(MAKE_DIRECTORY "${DIST}/library/${folder}")
        endif()
    endforeach()
endif()

foreach(disc IN LISTS kDiscs)
    if(EXISTS "${ROOT}/${disc}" AND NOT EXISTS "${DIST}/dsk/${disc}")
        file(COPY_FILE "${ROOT}/${disc}" "${DIST}/dsk/${disc}")
    endif()
endforeach()

# The cards' files, blank. An X-MASS disc is as long as what has been
# written on it: empty, every sector reads as zeros. An erased flash is all
# &FF: 32 places of 16K.
if(NOT EXISTS "${DIST}/img/xmass.img")
    file(TOUCH "${DIST}/img/xmass.img")
endif()
if(NOT EXISTS "${DIST}/img/xmem.flash")
    string(ASCII 255 erased)
    string(REPEAT "${erased}" 16384 rom)
    string(REPEAT "${rom}" 32 flash)
    file(WRITE "${DIST}/img/xmem.flash" "${flash}")
endif()
