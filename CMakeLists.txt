cmake_minimum_required(VERSION 3.16)
project(microscope_app_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(microscope_core
    src/treatment_factory.cpp
    src/levels_treatment.cpp
    src/echelle_treatment.cpp
    src/vue.cpp
)
target_include_directories(microscope_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(microscope_core PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE microscope_core)

# NOTE: this CMakeLists builds the core imaging logic only (Treatment
# pipeline, Levels, Échelle, Vue persistence) — no Qt, no camera capture.
# Those are the next slice (Qt MainWindow + OpenCV VideoCapture), deferred
# per the phased-delivery ADR 0009 until this core proves out.
