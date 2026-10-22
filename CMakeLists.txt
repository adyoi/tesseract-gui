cmake_minimum_required(VERSION 3.16)

project(tesseract-gui
    VERSION 0.1.0
    DESCRIPTION "Qt6 desktop front-end for tesseract-crypt (libtesseract_crypt)"
    HOMEPAGE_URL "https://github.com/adyoi/tesseract-gui"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TESSGUI_BUILD_TESTS "Build the test suite" ON)
option(TESSGUI_CONSOLE "Keep a console window (useful while debugging)" OFF)

# A console-subsystem binary makes Windows open a console for the process
# (a second `conhost.exe` plus a `ConsoleWindowClass` window).  That window
# sits behind the interface and looks like a stray prompt, so the default is
# a GUI-subsystem binary and the option only exists for debugging.
if(TESSGUI_CONSOLE)
    message(STATUS "tesseract-gui: console subsystem enabled - Windows will open an extra "
                   "console window behind the interface")
endif()

set(TESSGUI_TESSERACT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tesseract-crypt"
    CACHE PATH
    "tesseract-crypt source tree, used when no installed package is found")

# Helper scripts of this project.
set(TESSGUI_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------ #
# libtesseract_crypt                                                  #
# ------------------------------------------------------------------ #
# Two ways in, in this order: an installed tesseract-crypt package, or the
# source tree next to this one (one checkout of both repos is enough to build).
find_package(tesseract CONFIG QUIET)

if(TARGET tesseract::crypt)
    message(STATUS "libtesseract_crypt: using the installed tesseract package")
else()
    if(NOT EXISTS "${TESSGUI_TESSERACT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "libtesseract_crypt not found.\n"
            "  * install tesseract-crypt so that find_package(tesseract) works, or\n"
            "  * clone it next to this project, or point at the tree with\n"
            "    -DTESSGUI_TESSERACT_SOURCE_DIR=<path>.")
    endif()
    message(STATUS "libtesseract_crypt: building from ${TESSGUI_TESSERACT_SOURCE_DIR}")
    # Only the library: the GUI is the interface, the CLI and its test suite
    # are not needed here.
    set(TESS_BUILD_CLI OFF CACHE BOOL "Build the tesseract-crypt CLI" FORCE)
    set(TESS_BUILD_TESTS OFF CACHE BOOL "Build the tesseract-crypt test suite" FORCE)
    add_subdirectory("${TESSGUI_TESSERACT_SOURCE_DIR}"
                     "${CMAKE_CURRENT_BINARY_DIR}/tesseract-crypt"
                     EXCLUDE_FROM_ALL)
endif()

# ------------------------------------------------------------------ #
# libsodium runtime                                                   #
# ------------------------------------------------------------------ #
# When libtesseract_crypt is linked against an import library, the DLL has to
# travel next to our executables (the same thing the Windows ZIP of the CLI
# does).  The guesses below run *after* libtesseract_crypt has been found,
# because that is where SODIUM_LIBRARY gets decided; they can always be
# overridden with -DTESSGUI_SODIUM_DLL=<path to the DLL>.
set(TESSGUI_SODIUM_DLL "" CACHE FILEPATH
    "libsodium runtime DLL copied next to the executables (Windows)")

function(tessgui_bundle_sodium target)
    if(WIN32 AND TESSGUI_SODIUM_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${TESSGUI_SODIUM_DLL}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Bundling the libsodium runtime next to ${target}")
    endif()
endfunction()

# ------------------------------------------------------------------ #
# Windows runtime                                                     #
# ------------------------------------------------------------------ #
# The build tree has to be runnable without touching PATH: the MinGW
# runtime and the Qt libraries and plugins are copied next to the
# executables.  Without that, starting the application fails with
# "Qt6Core.dll not found".
option(TESSGUI_BUNDLE_RUNTIME
    "Copy the runtime libraries next to the executables (Windows)" ON)

function(tessgui_bundle_runtime target)
    if(NOT WIN32 OR NOT TESSGUI_BUNDLE_RUNTIME)
        return()
    endif()

    # The runtime libraries are collected from the folders that hold them:
    # the compiler (MinGW runtime), the Qt installation (Qt and its own
    # dependencies) and the libsodium runtime.
    set(_dirs "")
    get_filename_component(_tessgui_ccdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND _dirs "${_tessgui_ccdir}")
    if(QT6_INSTALL_PREFIX)
        list(APPEND _dirs "${QT6_INSTALL_PREFIX}/bin")
    endif()
    if(TESSGUI_SODIUM_DLL)
        get_filename_component(_tessgui_sodium_dir "${TESSGUI_SODIUM_DLL}" DIRECTORY)
        list(APPEND _dirs "${_tessgui_sodium_dir}")
    endif()
    list(REMOVE_DUPLICATES _dirs)

    # Qt libraries and plugins.
    find_program(TESSGUI_WINDEPLOYQT windeployqt6
        HINTS "${QT6_INSTALL_PREFIX}/bin"
        NO_DEFAULT_PATH)
    if(NOT TESSGUI_WINDEPLOYQT)
        find_program(TESSGUI_WINDEPLOYQT windeployqt6)
    endif()
    if(TESSGUI_WINDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${TESSGUI_WINDEPLOYQT}"
                --no-translations --no-system-d3d-compiler --no-opengl-sw
                --compiler-runtime
                --dir "$<TARGET_FILE_DIR:${target}>" "$<TARGET_FILE:${target}>"
            COMMENT "Deploying the Qt runtime next to ${target}")
    else()
        message(STATUS "windeployqt6: not found, the Qt runtime is not copied")
    endif()

    # The dependencies of the Qt libraries (ICU, freetype, harfbuzz, ...).
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DTESSGUI_BIN=$<TARGET_FILE:${target}>"
            "-DTESSGUI_DEST=$<TARGET_FILE_DIR:${target}>"
            "-DTESSGUI_SEARCH_DIRS=${_dirs}"
            -P "${TESSGUI_MODULE_DIR}/collect_runtime.cmake"
        COMMENT "Collecting the runtime libraries next to ${target}")

    # The offscreen platform plugin renders the screenshots without a
    # display; MSYS2 keeps the plugins below share/qt6/plugins.
    if(QT6_INSTALL_PREFIX)
        file(GLOB _tessgui_offscreen
            "${QT6_INSTALL_PREFIX}/plugins/platforms/qoffscreen.dll"
            "${QT6_INSTALL_PREFIX}/share/qt6/plugins/platforms/qoffscreen.dll")
        if(_tessgui_offscreen)
            list(GET _tessgui_offscreen 0 _tessgui_offscreen)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_tessgui_offscreen}"
                    "$<TARGET_FILE_DIR:${target}>/platforms/qoffscreen.dll"
                COMMENT "Bundling the offscreen platform plugin next to ${target}")
        endif()
    endif()
endfunction()

# libsodium: an import library means the DLL must be shipped next to us.
if(WIN32 AND NOT TESSGUI_SODIUM_DLL)
    set(_tessgui_sodium_candidates "")
    if(SODIUM_LIBRARY MATCHES "\\.dll\\.a$")
        get_filename_component(_sodium_libdir "${SODIUM_LIBRARY}" DIRECTORY)
        get_filename_component(_sodium_prefix "${_sodium_libdir}" DIRECTORY)
        file(GLOB _tessgui_sodium_candidates "${_sodium_prefix}/bin/*sodium*.dll")
    endif()
    if(NOT _tessgui_sodium_candidates)
        # MSYS2/MinGW layout: <prefix>/bin next to <prefix>/lib.  Harmless when
        # libsodium ended up linked statically: nothing loads the extra DLL.
        get_filename_component(_tessgui_ccdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(_tessgui_prefix "${_tessgui_ccdir}" DIRECTORY)
        file(GLOB _tessgui_sodium_candidates "${_tessgui_prefix}/bin/*sodium*.dll")
    endif()
    if(_tessgui_sodium_candidates)
        list(GET _tessgui_sodium_candidates 0 _tessgui_sodium_dll)
        set(TESSGUI_SODIUM_DLL "${_tessgui_sodium_dll}" CACHE FILEPATH
            "libsodium runtime DLL copied next to the executables (Windows)" FORCE)
        message(STATUS "libsodium runtime: ${_tessgui_sodium_dll}")
    else()
        message(STATUS "libsodium runtime: none found (static link assumed)")
    endif()
endif()

# ------------------------------------------------------------------ #
# Qt6                                                                 #
# ------------------------------------------------------------------ #
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent Svg)
if(TESSGUI_BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
endif()

if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
endif()

# ------------------------------------------------------------------ #
# application                                                         #
# ------------------------------------------------------------------ #
# The crypto layer is a small library of its own so the test suite can
# exercise exactly the code the interface runs, without a display.
add_library(tessgui-backend STATIC
    src/backend/tessbackend.cpp)

target_include_directories(tessgui-backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(tessgui-backend PUBLIC
    Qt6::Core
    tesseract::crypt)

set(TESSGUI_SOURCES
    src/main.cpp
    src/iconexport.cpp
    src/ui/aboutdialog.cpp
    src/ui/filerow.cpp
    src/ui/mainwindow.cpp
    src/ui/keygendialog.cpp
    src/ui/optionsdialog.cpp
    src/ui/toolsdialogs.cpp
    src/ui/textcryptdialog.cpp
)

qt_add_executable(tesseract-gui ${TESSGUI_SOURCES})

# The logo of tesseract-crypt, compiled into the program.
qt_add_resources(tesseract-gui "app"
    PREFIX "/"
    BASE "assets"
    FILES assets/logo.svg)

target_link_libraries(tesseract-gui PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
    tessgui-backend)

# The icon of the executable comes from the same artwork as the window.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/tesseract-gui.ico")
    enable_language(RC)
    target_sources(tesseract-gui PRIVATE assets/tesseract-gui.rc)
endif()

tessgui_bundle_sodium(tesseract-gui)
tessgui_bundle_runtime(tesseract-gui)

set_target_properties(tesseract-gui PROPERTIES
    OUTPUT_NAME tesseract-gui
    WIN32_EXECUTABLE $<NOT:$<BOOL:${TESSGUI_CONSOLE}>>)

if(MSVC)
    target_compile_options(tesseract-gui PRIVATE /W4)
    target_compile_definitions(tesseract-gui PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(tesseract-gui PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------ #
# install                                                             #
# ------------------------------------------------------------------ #
include(GNUInstallDirs)

install(TARGETS tesseract-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ------------------------------------------------------------------ #
# tests                                                               #
# ------------------------------------------------------------------ #
if(TESSGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
