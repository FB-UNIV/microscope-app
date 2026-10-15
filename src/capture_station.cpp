#include "capture_station.hpp"
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
    // Always set both, even "auto": the camera otherwise keeps whatever the
    // last program (or the last session) left behind.
    ProfileReport report;
    report.exposureApplied =
        config_.exposureMs ? camera_->setExposureMs(*config_.exposureMs) : camera_->enableAutoExposure();
    report.whiteBalanceApplied = config_.whiteBalanceKelvin
                                     ? camera_->setWhiteBalanceKelvin(*config_.whiteBalanceKelvin)
                                     : camera_->enableAutoWhiteBalance();
    return report;
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

Vue CaptureStation::capture(const std::filesystem::path& imagePath) {
    CaptureInfo info;
    info.cameraBackend = camera_->backend();
    info.exposureMs = config_.exposureMs;
    info.whiteBalanceKelvin = config_.whiteBalanceKelvin;
    info.whiteReference = config_.whiteReference;
    return Vue::capture(grabPreview(), imagePath, info);
}

WhiteBalance CaptureStation::calibrateWhiteReference() {
    config_.whiteReference = computeWhiteReferenceGains(camera_->grab());
    return config_.whiteReference;
}

} // namespace msapp
