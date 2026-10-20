#include "capture_station.hpp"
#include "echelle_treatment.hpp"
#include "white_reference.hpp"

#include <stdexcept>
#include <string>

namespace msapp {

CaptureStation::CaptureStation(CameraConfig config)
    : CaptureStation(makeCamera(config), config) {}

CaptureStation::CaptureStation(std::unique_ptr<ICamera> camera, CameraConfig config)
    : camera_(std::move(camera)), config_(std::move(config)) {
    if (!camera_) {
        throw std::invalid_argument("CaptureStation: camera must not be null");
    }
}

ProfileReport CaptureStation::open() {
    camera_->open();
    // A stream that just started may send empty frames: warm-up allowance.
    liveFrameSeen_ = false;
    failedLiveGrabs_ = 0;
    // Always set both, even "auto": the camera otherwise keeps whatever the
    // last program (or the last session) left behind.
    ProfileReport report;
    report.exposureApplied =
        config_.exposureMs ? camera_->setExposureMs(*config_.exposureMs) : camera_->enableAutoExposure();
    report.whiteBalanceApplied = config_.whiteBalanceKelvin
                                     ? camera_->setWhiteBalanceKelvin(*config_.whiteBalanceKelvin)
                                     : camera_->enableAutoWhiteBalance();
    // Read the mode back instead of trusting the request: drivers fall back
    // to another mode silently, which would invalidate every Calibration
    // measured in the profile's mode (ADR 0015).
    report.frameSize = camera_->frameSize();
    report.pixelFormat = camera_->pixelFormat();
    const cv::Size requested(config_.width, config_.height);
    report.frameSizeApplied = requested.empty() || report.frameSize == requested;
    report.pixelFormatApplied = config_.pixelFormat.empty() || report.pixelFormat == config_.pixelFormat;
    report_ = report;
    return report;
}

void CaptureStation::close() {
    camera_->close();
}

bool CaptureStation::isOpen() const {
    return camera_->isOpen();
}

cv::Mat CaptureStation::grabFrame() {
    cv::Mat frame = camera_->grab();
    // ICamera never returns an empty frame; a backend that does anyway must
    // fail the capture, not store an empty Vue.
    if (frame.empty()) {
        throw std::runtime_error("CaptureStation: the " + camera_->backend() + " camera returned an empty frame");
    }
    return frame;
}

std::optional<cv::Mat> CaptureStation::grabLiveFrame() {
    try {
        cv::Mat frame = grabFrame();
        liveFrameSeen_ = true;
        failedLiveGrabs_ = 0;
        return frame;
    } catch (const std::exception& e) {
        const int allowed = liveFrameSeen_ ? kDroppedGrabs : kWarmUpGrabs;
        if (++failedLiveGrabs_ < allowed) return std::nullopt;
        failedLiveGrabs_ = 0;
        throw std::runtime_error("CaptureStation: no frame from the camera in " + std::to_string(allowed) +
                                 " grabs in a row" + (liveFrameSeen_ ? "" : " since it opened") + " (" + e.what() +
                                 ")");
    }
}

cv::Mat CaptureStation::grabPreview() {
    return applyWhiteBalanceGains(grabFrame(), config_.whiteReference);
}

std::optional<CaptureStation::LivePreview> CaptureStation::grabLivePreview(cv::Size fitWithin) {
    const std::optional<cv::Mat> frame = grabLiveFrame();
    if (!frame) return std::nullopt;
    return LivePreview{whiteBalancedPreview(*frame, config_.whiteReference, fitWithin), frame->size()};
}

bool CaptureStation::dropFrame() {
    return grabLiveFrame().has_value();
}

Vue CaptureStation::capture(const std::filesystem::path& imagePath, const std::optional<Calibration>& calibration) {
    const cv::Mat frame = grabPreview();
    CaptureInfo info;
    info.cameraBackend = camera_->backend();
    // What the camera accepted at open(), not what the profile asked for: a
    // refused setting (or auto) left the value unknown, so it stays null.
    if (report_.exposureApplied) info.exposureMs = config_.exposureMs;
    if (report_.whiteBalanceApplied) info.whiteBalanceKelvin = config_.whiteBalanceKelvin;
    info.whiteReference = config_.whiteReference;
    info.frameWidth = frame.cols;
    info.frameHeight = frame.rows;
    info.pixelFormat = camera_->pixelFormat();
    // Vue::capture refuses a Calibration measured at another frame size.
    Vue vue = Vue::capture(frame, imagePath, info, calibration);
    // Every calibrated capture gets its scale bar straight away (ADR 0017).
    // It is a Treatment in the sidecar, never baked into the original.
    if (calibration) {
        vue.addTreatment(std::make_unique<EchelleTreatment>(*calibration));
    }
    return vue;
}

WhiteBalance CaptureStation::calibrateWhiteReference() {
    config_.whiteReference = computeWhiteReferenceGains(grabFrame());
    return config_.whiteReference;
}

} // namespace msapp
