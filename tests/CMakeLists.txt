# ------------------------------------------------------------------ #
# test suite                                                          #
# ------------------------------------------------------------------ #
# The tests run without a display: they call the same backend functions the
# interface uses.  One extra test renders a window with the offscreen Qt
# platform, which catches broken layouts and missing widgets in CI.

qt_add_executable(tessgui-tests test_backend.cpp)

# A test binary needs a console: without one the platform swallows the
# per-test output that makes a CI failure readable.
set_target_properties(tessgui-tests PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE)

target_link_libraries(tessgui-tests PRIVATE
    Qt6::Test
    tessgui-backend)

tessgui_bundle_sodium(tessgui-tests)
tessgui_bundle_runtime(tessgui-tests)

add_test(NAME backend COMMAND tessgui-tests
         -o "${CMAKE_CURRENT_BINARY_DIR}/backend.log,txt")

add_test(NAME screenshots COMMAND tesseract-gui --screenshot
         "${CMAKE_CURRENT_BINARY_DIR}/screenshot-main.png" --mode decrypt)
set_tests_properties(screenshots PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Qt's runtime libraries live next to the compiler in MSYS2-like layouts;
# make sure the tests find them even when run from a plain shell.
if(WIN32 AND DEFINED QT6_INSTALL_PREFIX)
    get_filename_component(_tessgui_qt_bin "${QT6_INSTALL_PREFIX}/bin" ABSOLUTE)
    if(EXISTS "${_tessgui_qt_bin}")
        set_tests_properties(backend screenshots PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_tessgui_qt_bin}")
    endif()
endif()
