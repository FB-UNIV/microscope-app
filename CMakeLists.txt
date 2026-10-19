cmake_minimum_required(VERSION 3.16)
project(microscope_app_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sources and test strings are UTF-8 (Échantillon, Échelle); MSVC would
# otherwise read them in the local ANSI code page.
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)

# Which build this is, shown by microscope_app (title bar, --version) and
# camera_probe, and used in the release zip names: the tag (v0.1.0), the
# commits since it (v0.1.0-3-gabc1234), the short commit before any tag,
# "-dirty" with uncommitted changes. Read at configure time: re-run CMake to
# refresh it. MSAPP_VERSION overrides it; a tree without git says "unknown".
set(MSAPP_VERSION "" CACHE STRING "Build version shown by the app and camera_probe (empty: git describe)")
set(msapp_version "${MSAPP_VERSION}")
if(NOT msapp_version)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --always --dirty
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                        RESULT_VARIABLE git_result
                        OUTPUT_VARIABLE msapp_version
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(NOT git_result EQUAL 0)
            set(msapp_version "")
        endif()
    endif()
endif()
if(NOT msapp_version)
    set(msapp_version "unknown")
endif()
message(STATUS "microscope-app build version: ${msapp_version}")
configure_file(cmake/build_version.hpp.in generated/build_version.hpp @ONLY)

add_library(microscope_core
    src/treatment_factory.cpp
    src/format_microns.cpp
    src/levels_treatment.cpp
    src/echelle_treatment.cpp
    src/vue.cpp
    src/measurement.cpp
    src/phase_analysis.cpp
    src/phase_overlay_treatment.cpp
    src/vickers.cpp
    src/measurement_marks.cpp
    src/file_replay_camera.cpp
    src/opencv_camera.cpp
    src/camera_factory.cpp
    src/white_reference.cpp
    src/capture_station.cpp
    src/safe_write.cpp
    src/workstation_config.cpp
    src/vue_store.cpp
    src/catalog.cpp
    src/mire_calibration.cpp
    src/fiche.cpp
)
# generated/ holds build_version.hpp, for the app and camera_probe.
target_include_directories(microscope_core PUBLIC include ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(microscope_core PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)

# Phase analysis must reproduce the legacy float arithmetic step by step
# (ADR 0012); fused multiply-add would change rounding, so forbid it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(microscope_core PRIVATE -ffp-contract=off)
elseif(MSVC)
    target_compile_options(microscope_core PRIVATE /fp:precise)
endif()

# Optional: folder holding the legacy CTTM*.tif archives. When set, the
# legacy-parity checks run; otherwise they print [SKIP].
set(MSAPP_LEGACY_DATA_DIR "" CACHE PATH "Folder with legacy Metal view TIFFs (optional)")

enable_testing()
foreach(test_name test_pipeline test_measurement test_phase_analysis test_vickers test_camera
                  test_white_reference test_capture_station test_treatment_registration
                  test_workstation_config test_vue_store test_catalog test_mire_calibration
                  test_vue_editing test_fiche test_measurement_marks)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE microscope_core)
    target_compile_definitions(${test_name} PRIVATE MSAPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSAPP_LEGACY_DATA_DIR)
        add_test(NAME ${test_name} COMMAND ${test_name} ${MSAPP_LEGACY_DATA_DIR})
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endforeach()

# Hardware check for lab PCs (needs a camera, so it is built but not run
# by CTest): lists cameras, frame size, pixel format, saves a test frame.
add_executable(camera_probe tools/camera_probe.cpp)
target_link_libraries(camera_probe PRIVATE microscope_core)
add_test(NAME camera_probe_version COMMAND camera_probe --version)
set_tests_properties(camera_probe_version PROPERTIES PASS_REGULAR_EXPRESSION "^camera_probe ${msapp_version}")

# The Qt 6 app (app/). Qt stays out of microscope_core: turn this off to
# build and test the core on a machine without Qt.
option(MSAPP_BUILD_APP "Build the Qt 6 desktop app (needs Qt 6 Widgets)" ON)
if(MSAPP_BUILD_APP)
    add_subdirectory(app)
endif()
