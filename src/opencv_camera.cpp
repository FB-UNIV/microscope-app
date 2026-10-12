#include "opencv_camera.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace msapp {

OpenCvCamera::OpenCvCamera(int deviceIndex, int apiPreference)
    : deviceIndex_(deviceIndex), apiPreference_(apiPreference) {}

void OpenCvCamera::open() {
    if (capture_.isOpened()) return;
    if (!capture_.open(deviceIndex_, apiPreference_)) {
        throw std::runtime_error("OpenCvCamera: no UVC camera at index " + std::to_string(deviceIndex_) +
                                 ". Motic USB cameras use the WinUSB driver and need the Motic SDK "
                                 "backend instead (ADR 0011).");
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

} // namespace msapp
