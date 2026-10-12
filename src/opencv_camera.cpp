#include "opencv_camera.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace msapp {

OpenCvCamera::OpenCvCamera(int deviceIndex, int apiPreference, cv::Size requestedSize)
    : deviceIndex_(deviceIndex), apiPreference_(apiPreference), requestedSize_(requestedSize) {}

void OpenCvCamera::open() {
    if (capture_.isOpened()) return;
    if (!capture_.open(deviceIndex_, apiPreference_)) {
        throw std::runtime_error("OpenCvCamera: no camera at index " + std::to_string(deviceIndex_) +
                                 ". Check it is plugged in and not held by another program (e.g. "
                                 "MotiConnect). The Moticam A5 is UVC and should appear here "
                                 "(ADR 0013); WinUSB-only Motic models do not (ADR 0011).");
    }
    if (!requestedSize_.empty()) {
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, requestedSize_.width);
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, requestedSize_.height);
    }
}

void OpenCvCamera::close() {
    capture_.release();
}

cv::Mat OpenCvCamera::grab() {
    if (!capture_.isOpened()) {
        throw std::runtime_error("OpenCvCamera::grab: camera is not open");
    }
    cv::Mat frame;
    if (!capture_.read(frame) || frame.empty()) {
        throw std::runtime_error("OpenCvCamera::grab: device returned no frame");
    }
    return frame;
}

bool OpenCvCamera::setExposureMs(double milliseconds) {
    if (!capture_.isOpened() || milliseconds <= 0.0) return false;
    const int api = static_cast<int>(capture_.get(cv::CAP_PROP_BACKEND));
    const bool logSeconds = api == cv::CAP_DSHOW || api == cv::CAP_MSMF;
    const double value = logSeconds ? std::log2(milliseconds / 1000.0) : milliseconds;
    return capture_.set(cv::CAP_PROP_EXPOSURE, value);
}

bool OpenCvCamera::setGain(double gain) {
    if (!capture_.isOpened()) return false;
    return capture_.set(cv::CAP_PROP_GAIN, gain);
}

cv::Size OpenCvCamera::frameSize() const {
    if (!capture_.isOpened()) return {};
    return {static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH)),
            static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT))};
}

std::string OpenCvCamera::captureApiName() const {
    if (!capture_.isOpened()) return {};
    return capture_.getBackendName();
}

} // namespace msapp
