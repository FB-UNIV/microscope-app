#pragma once

#include "camera.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace msapp {

// Which capture backend a workstation uses — chosen by configuration, not
// compiled in, so the same build can run a lab PC (Motic), a laptop webcam
// demo (OpenCV) or a no-camera session (file replay).
struct CameraConfig {
    std::string backend = "file";               // "file" | "opencv" | "motic"
    int deviceIndex = 0;                         // opencv
    std::vector<std::filesystem::path> frames;   // file
};

// Throws std::invalid_argument for an unknown backend, and
// std::runtime_error for "motic" until the Motic SDK backend exists (ADR 0011).
std::unique_ptr<ICamera> makeCamera(const CameraConfig& config);

} // namespace msapp
