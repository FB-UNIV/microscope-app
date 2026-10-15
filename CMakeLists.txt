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
    src/file_replay_camera.cpp
    src/opencv_camera.cpp
    src/camera_factory.cpp
    src/white_reference.cpp
    src/capture_station.cpp
)
target_include_directories(microscope_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
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
                  test_white_reference test_capture_station test_treatment_registration)
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

# The Qt 6 app (app/). Qt stays out of microscope_core: turn this off to
# build and test the core on a machine without Qt.
option(MSAPP_BUILD_APP "Build the Qt 6 desktop app (needs Qt 6 Widgets)" ON)
if(MSAPP_BUILD_APP)
    add_subdirectory(app)
endif()
