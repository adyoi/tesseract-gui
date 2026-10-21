# collect_runtime.cmake
#
# Copies the DLL closure of a binary next to it, so that the build tree can
# be started without a prepared PATH.  windeployqt only brings the Qt modules
# and the plugins; the libraries Qt itself depends on (ICU, freetype,
# harfbuzz, brotli, ...) live next to it in the same folder and have to be
# collected as well.
#
# The import table of every file is read with objdump (MinGW) or dumpbin
# (MSVC); the imported DLLs are looked up in the given search folders and
# copied, and the search continues with the copies.
#
# Required variables:
#   TESSGUI_BIN          the binary to start from
#   TESSGUI_DEST         folder the DLLs are copied into
#   TESSGUI_SEARCH_DIRS  list of folders the DLLs are looked up in
#
# SPDX-License-Identifier: MIT

if(NOT DEFINED TESSGUI_BIN OR NOT DEFINED TESSGUI_DEST)
    message(FATAL_ERROR "collect_runtime: TESSGUI_BIN and TESSGUI_DEST are required")
endif()

set(_search_dirs "")
if(DEFINED TESSGUI_SEARCH_DIRS)
    set(_search_dirs ${TESSGUI_SEARCH_DIRS})
endif()

# ------------------------------------------------------------------ #
# the tool that reads the import table                                #
# ------------------------------------------------------------------ #
set(_tool "")
set(_tool_args "")
set(_tool_mode "objdump")

foreach(_dir IN LISTS _search_dirs)
    if(EXISTS "${_dir}/objdump.exe")
        set(_tool "${_dir}/objdump.exe")
        set(_tool_args -p)
        break()
    endif()
endforeach()

if(NOT _tool)
    find_program(_tool NAMES objdump)
    if(_tool)
        set(_tool_args -p)
    endif()
endif()

if(NOT _tool)
    find_program(_tool NAMES dumpbin)
    if(_tool)
        set(_tool_args /dependents)
        set(_tool_mode "dumpbin")
    endif()
endif()

if(NOT _tool)
    message(STATUS "collect_runtime: no import reader found, the runtime is not collected")
    return()
endif()

# ------------------------------------------------------------------ #
# the walk                                                            #
# ------------------------------------------------------------------ #
set(_pending "${TESSGUI_BIN}")
set(_seen "")
set(_copied "")

while(_pending)
    list(POP_FRONT _pending _current)
    if(_current IN_LIST _seen)
        continue()
    endif()
    list(APPEND _seen "${_current}")

    execute_process(
        COMMAND "${_tool}" ${_tool_args} "${_current}"
        OUTPUT_VARIABLE _out
        RESULT_VARIABLE _rc
        ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        continue()
    endif()

    set(_dlls "")
    if(_tool_mode STREQUAL "objdump")
        string(REGEX MATCHALL "DLL Name: *([^\r\n]+)" _matches "${_out}")
        foreach(_match IN LISTS _matches)
            string(REGEX REPLACE "DLL Name: *([^\r\n]+)" "\\1" _dll "${_match}")
            string(STRIP "${_dll}" _dll)
            if(NOT _dll STREQUAL "")
                list(APPEND _dlls "${_dll}")
            endif()
        endforeach()
    else()
        string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.dll" _matches "${_out}")
        foreach(_match IN LISTS _matches)
            list(APPEND _dlls "${_match}")
        endforeach()
    endif()

    foreach(_dll IN LISTS _dlls)
        if(_dll IN_LIST _seen)
            continue()
        endif()
        set(_found "")
        foreach(_dir IN LISTS _search_dirs)
            if(EXISTS "${_dir}/${_dll}")
                set(_found "${_dir}/${_dll}")
                break()
            endif()
        endforeach()
        if(NOT _found)
            # A system library: the loader finds it on its own.
            continue()
        endif()
        get_filename_component(_name "${_found}" NAME)
        if(NOT EXISTS "${TESSGUI_DEST}/${_name}")
            file(COPY "${_found}" DESTINATION "${TESSGUI_DEST}")
            list(APPEND _copied "${_name}")
        endif()
        # Continue with the library itself, it has dependencies of its own.
        list(APPEND _pending "${_found}")
    endforeach()
endwhile()

if(_copied)
    list(LENGTH _copied _count)
    message(STATUS "collect_runtime: copied ${_count} runtime libraries next to ${TESSGUI_BIN}")
endif()
