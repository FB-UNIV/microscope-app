#include "camera_factory.hpp"
#include "file_replay_camera.hpp"
#include "opencv_camera.hpp"

#include <stdexcept>

namespace msapp {

int captureApiFromName(const std::string& name) {
    if (name == "auto") return cv::CAP_ANY;
    if (name == "dshow") return cv::CAP_DSHOW;
    if (name == "msmf") return cv::CAP_MSMF;
    if (name == "v4l2") return cv::CAP_V4L2;
    throw std::invalid_argument("captureApiFromName: unknown capture API '" + name +
                                "' (expected auto, dshow, msmf or v4l2)");
}

std::unique_ptr<ICamera> makeCamera(const CameraConfig& config) {
    if (config.backend == "file") {
        return std::make_unique<FileReplayCamera>(config.frames);
    }
    if (config.backend == "opencv") {
        return std::make_unique<OpenCvCamera>(config.deviceIndex, captureApiFromName(config.captureApi),
                                              cv::Size(config.width, config.height));
    }
    if (config.backend == "motic") {
        throw std::runtime_error(
            "makeCamera: there is no Motic SDK backend. The Moticam A5 is a UVC camera — use backend "
            "\"opencv\" (ADR 0013). An SDK backend is only needed for WinUSB-only Motic models (ADR 0011).");
    }
    throw std::invalid_argument("makeCamera: unknown camera backend '" + config.backend + "'");
}

} // namespace msapp
