# The Qt 6 desktop app (ADR 0001, ADR 0009). Qt is linked here only:
# microscope_core stays free of Qt, so the core still builds and tests
# with -DMSAPP_BUILD_APP=OFF on a machine without Qt.

# 6.4 is what Debian 12 and Ubuntu 24.04 ship (the Linux CI job builds
# against it); the Windows bundle uses the Qt pinned in CI.
# Test (QtTest, part of qtbase like Widgets) runs the widget tests below.
find_package(Qt6 6.4 COMPONENTS Widgets Test)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt 6 (Widgets, Test) not found. Point CMAKE_PREFIX_PATH at a Qt 6 install, "
                        "or configure with -DMSAPP_BUILD_APP=OFF to build only the core and its tests.")
endif()
qt_standard_project_setup()

qt_add_executable(microscope_app
    main.cpp
    main_window.cpp
    main_window.hpp
    location_picker.cpp
    location_picker.hpp
    mire_dialog.cpp
    mire_dialog.hpp
    fiche_pdf.cpp
    fiche_pdf.hpp
    histogram_widget.cpp
    histogram_widget.hpp
    image_view.cpp
    image_view.hpp
    preview_widget.cpp
    preview_widget.hpp
    preview_worker.cpp
    preview_worker.hpp
    qt_image.cpp
    qt_image.hpp
    vue_window.cpp
    vue_window.hpp
    wait_cursor.hpp
)
target_link_libraries(microscope_app PRIVATE microscope_core Qt6::Widgets)
set_target_properties(microscope_app PROPERTIES WIN32_EXECUTABLE ON)

# Launch check: shows the MainWindow, then quits once the event loop runs.
# Offscreen, so it needs no display (CI, WSL). The Windows job also runs it
# from the windeployqt bundle with the real platform plugin.
add_test(NAME microscope_app_launch COMMAND microscope_app --smoke-test)
set_tests_properties(microscope_app_launch PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 60)

# Widget tests (QtTest): the mouse gestures of the image view — click, pan,
# handle drag and right-click, rectangle mode. Offscreen, like the launch check.
qt_add_executable(test_image_view
    tests/test_image_view.cpp
    image_view.cpp
    image_view.hpp
)
target_include_directories(test_image_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_image_view PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME test_image_view COMMAND test_image_view)
set_tests_properties(test_image_view PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 60)
