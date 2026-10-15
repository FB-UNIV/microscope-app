#pragma once

#include "camera.hpp"
#include "white_balance.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// One workstation's camera profile — chosen by configuration, not compiled
// in, so the same build can run a lab PC (Moticam A5 via "opencv", ADR
// 0013), a laptop webcam demo, or a no-camera session (file replay).
// Saved as JSON in the workstation config. Exposure and colour temperature
// are re-applied by CaptureStation on every open: the A5 keeps whatever the
// last program set (docs/hardware/camera-probe.md).
struct CameraConfig {
    std::string backend = "file";               // "file" | "opencv" | "motic"
    int deviceIndex = 0;                         // opencv
    // opencv: "auto" | "dshow" | "msmf" | "v4l2". On Windows "dshow" exposes
    // exposure and white balance; "msmf" is OpenCV's default there.
    std::string captureApi = "auto";
    int width = 0;                               // opencv: requested frame size,
    int height = 0;                              //   0 = keep the device default
    std::string pixelFormat;                     // opencv: FOURCC, e.g. "MJPG"; empty = device default
    std::optional<double> exposureMs;            // manual exposure; empty = camera auto (best effort)
    std::optional<double> whiteBalanceKelvin;    // locked colour temperature; empty = camera auto WB
    WhiteBalance whiteReference;                 // gains baked into captures (ADR 0014); identity until calibrated
    std::vector<std::filesystem::path> frames;   // file

    nlohmann::json toJson() const;
    // Missing optional keys take their defaults, so older config files load.
    static CameraConfig fromJson(const nlohmann::json& j);
};

// The Moticam A5 as measured with tools/camera_probe (two lab runs,
// docs/hardware/camera-probe.md): DirectShow, the only API exposing white
// balance; 3840x2160 MJPG — its HD modes are MJPG anyway, and the frame rate
// is exposure-bound, so one 4K stream serves preview and capture; colour
// temperature locked at the camera's own 5000 K default to stop auto-WB
// drift. Exposure stays with the camera until measured through a microscope;
// the white reference stays identity until calibrated on each microscope.
CameraConfig moticamA5Profile();

// Maps a CameraConfig::captureApi name to the OpenCV cv::CAP_* id.
// Throws std::invalid_argument for an unknown name.
int captureApiFromName(const std::string& name);

// Throws std::invalid_argument for an unknown backend, capture API or pixel
// format, and std::runtime_error for "motic": no SDK backend exists, and the
// Moticam A5 does not need one (ADR 0013).
std::unique_ptr<ICamera> makeCamera(const CameraConfig& config);

} // namespace msapp
