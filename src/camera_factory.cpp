#include "camera_factory.hpp"
#include "json_optional.hpp"
#include "file_replay_camera.hpp"
#include "opencv_camera.hpp"

#include <stdexcept>

namespace msapp {

using detail::optionalFromJson;
using detail::optionalToJson;

nlohmann::json CameraConfig::toJson() const {
    nlohmann::json framesJson = nlohmann::json::array();
    for (const auto& frame : frames) framesJson.push_back(frame.u8string());
    return {
        {"backend", backend},
        {"device_index", deviceIndex},
        {"capture_api", captureApi},
        {"width", width},
        {"height", height},
        {"pixel_format", pixelFormat},
        {"exposure_ms", optionalToJson(exposureMs)},
        {"white_balance_kelvin", optionalToJson(whiteBalanceKelvin)},
        {"white_reference", whiteReference.toJson()},
        {"frames", framesJson},
    };
}

CameraConfig CameraConfig::fromJson(const nlohmann::json& j) {
    CameraConfig c;
    c.backend = j.at("backend").get<std::string>();
    c.deviceIndex = j.value("device_index", c.deviceIndex);
    c.captureApi = j.value("capture_api", c.captureApi);
    c.width = j.value("width", c.width);
    c.height = j.value("height", c.height);
    c.pixelFormat = j.value("pixel_format", c.pixelFormat);
    c.exposureMs = optionalFromJson(j, "exposure_ms");
    c.whiteBalanceKelvin = optionalFromJson(j, "white_balance_kelvin");
    if (j.contains("white_reference")) c.whiteReference = WhiteBalance::fromJson(j.at("white_reference"));
    if (j.contains("frames")) {
        for (const auto& frame : j.at("frames")) c.frames.push_back(std::filesystem::u8path(frame.get<std::string>()));
    }
    return c;
}

CameraConfig moticamA5Profile() {
    CameraConfig c;
    c.backend = "opencv";
    c.deviceIndex = 0;
    c.captureApi = "dshow";
    c.width = 3840;
    c.height = 2160;
    c.pixelFormat = "MJPG";
    c.exposureMs = 7.8125;  // 2^-7 s: DirectShow's -7
    c.whiteBalanceKelvin = 5000.0;
    return c;
}

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
                                              cv::Size(config.width, config.height), config.pixelFormat);
    }
    if (config.backend == "motic") {
        throw std::runtime_error(
            "makeCamera: there is no Motic SDK backend. The Moticam A5 is a UVC camera — use backend "
            "\"opencv\" (ADR 0013). An SDK backend is only needed for WinUSB-only Motic models (ADR 0011).");
    }
    throw std::invalid_argument("makeCamera: unknown camera backend '" + config.backend + "'");
}

} // namespace msapp
