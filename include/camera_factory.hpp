#pragma once

#include "camera.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace msapp {

// Which capture backend a workstation uses — chosen by configuration, not
// compiled in, so the same build can run a lab PC (Moticam A5 via "opencv",
// ADR 0013), a laptop webcam demo, or a no-camera session (file replay).
struct CameraConfig {
    std::string backend = "file";               // "file" | "opencv" | "motic"
    int deviceIndex = 0;                         // opencv
    // opencv: "auto" | "dshow" | "msmf" | "v4l2". On Windows "dshow" exposes
    // exposure/gain on most UVC cameras; "msmf" is OpenCV's default there.
    std::string captureApi = "auto";
    int width = 0;                               // opencv: requested frame size,
    int height = 0;                              //   0 = keep the device default
    std::vector<std::filesystem::path> frames;   // file
};

// Maps a CameraConfig::captureApi name to the OpenCV cv::CAP_* id.
// Throws std::invalid_argument for an unknown name.
int captureApiFromName(const std::string& name);

// Throws std::invalid_argument for an unknown backend or capture API, and
// std::runtime_error for "motic": no SDK backend exists, and the Moticam A5
// does not need one (ADR 0013).
std::unique_ptr<ICamera> makeCamera(const CameraConfig& config);

} // namespace msapp
