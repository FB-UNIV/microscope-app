#include "capture_station.hpp"
#include "echelle_treatment.hpp"
#include "white_reference.hpp"

#include <stdexcept>

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
    report_ = ProfileReport();
    applyControls();
    // Read the mode back instead of trusting the request: drivers fall back
    // to another mode silently, which would invalidate every Calibration
    // measured in the profile's mode (ADR 0015).
    report_.frameSize = camera_->frameSize();
    report_.pixelFormat = camera_->pixelFormat();
    const cv::Size requested(config_.width, config_.height);
    report_.frameSizeApplied = requested.empty() || report_.frameSize == requested;
    report_.pixelFormatApplied = config_.pixelFormat.empty() || report_.pixelFormat == config_.pixelFormat;
    return report_;
}

ProfileReport CaptureStation::setControls(std::optional<double> exposureMs, std::optional<double> whiteBalanceKelvin) {
    if (!camera_->isOpen()) {
        throw std::runtime_error("CaptureStation::setControls: the camera is not open");
    }
    config_.exposureMs = exposureMs;
    config_.whiteBalanceKelvin = whiteBalanceKelvin;
    applyControls();
    return report_;
}

void CaptureStation::applyControls() {
    // Always set both, even "auto": the camera otherwise keeps whatever the
    // last program (or the last session) left behind.
    report_.exposureApplied =
        config_.exposureMs ? camera_->setExposureMs(*config_.exposureMs) : camera_->enableAutoExposure();
    report_.whiteBalanceApplied = config_.whiteBalanceKelvin
                                      ? camera_->setWhiteBalanceKelvin(*config_.whiteBalanceKelvin)
                                      : camera_->enableAutoWhiteBalance();
}

void CaptureStation::close() {
    camera_->close();
}

bool CaptureStation::isOpen() const {
    return camera_->isOpen();
}

cv::Mat CaptureStation::grabPreview() {
    return applyWhiteBalanceGains(camera_->grab(), config_.whiteReference);
}

CaptureStation::LivePreview CaptureStation::grabLivePreview(cv::Size fitWithin) {
    const cv::Mat frame = camera_->grab();
    return {whiteBalancedPreview(frame, config_.whiteReference, fitWithin), frame.size()};
}

void CaptureStation::dropFrame() {
    camera_->grab();
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
    config_.whiteReference = computeWhiteReferenceGains(camera_->grab());
    return config_.whiteReference;
}

} // namespace msapp
