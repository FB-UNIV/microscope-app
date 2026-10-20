#include "opencv_camera.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace msapp {

OpenCvCamera::OpenCvCamera(int deviceIndex, int apiPreference, cv::Size requestedSize, std::string pixelFormat)
    : deviceIndex_(deviceIndex),
      apiPreference_(apiPreference),
      requestedSize_(requestedSize),
      pixelFormat_(std::move(pixelFormat)) {
    if (!pixelFormat_.empty() && pixelFormat_.size() != 4) {
        throw std::invalid_argument("OpenCvCamera: pixel format must be a 4-character FOURCC like \"MJPG\", got '" +
                                    pixelFormat_ + "'");
    }
}

void OpenCvCamera::open() {
    if (capture_.isOpened()) return;
    if (!capture_.open(deviceIndex_, apiPreference_)) {
        // The Moticam A5 is UVC and appears here (ADR 0013); WinUSB-only
        // Motic models never do (ADR 0011).
        throw std::runtime_error("OpenCvCamera: no camera at index " + std::to_string(deviceIndex_) +
                                 ". Check it is plugged in and not held by another program (e.g. "
                                 "MotiConnect).");
    }
    // Format before size: some drivers only offer the large modes in MJPG.
    // set()'s return values are not trusted: drivers report success and fall
    // back to another mode. frameSize() and pixelFormat() read back what was
    // granted; CaptureStation checks them against the profile (ADR 0015).
    if (!pixelFormat_.empty()) {
        capture_.set(cv::CAP_PROP_FOURCC,
                     cv::VideoWriter::fourcc(pixelFormat_[0], pixelFormat_[1], pixelFormat_[2], pixelFormat_[3]));
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

bool OpenCvCamera::enableAutoExposure() {
    if (!capture_.isOpened()) return false;
    // V4L2 uses 3 ("aperture priority") for auto; the Windows backends take 1.
    const int api = static_cast<int>(capture_.get(cv::CAP_PROP_BACKEND));
    return capture_.set(cv::CAP_PROP_AUTO_EXPOSURE, api == cv::CAP_V4L2 ? 3 : 1);
}

bool OpenCvCamera::setGain(double gain) {
    if (!capture_.isOpened()) return false;
    return capture_.set(cv::CAP_PROP_GAIN, gain);
}

bool OpenCvCamera::setWhiteBalanceKelvin(double kelvin) {
    if (!capture_.isOpened() || kelvin <= 0.0) return false;
    capture_.set(cv::CAP_PROP_AUTO_WB, 0);
    capture_.set(cv::CAP_PROP_WB_TEMPERATURE, kelvin);
    // Trust the read-back, not set()'s return value: drivers accept and
    // silently clamp or ignore values outside their range.
    return std::abs(capture_.get(cv::CAP_PROP_WB_TEMPERATURE) - kelvin) < 1.0;
}

bool OpenCvCamera::enableAutoWhiteBalance() {
    if (!capture_.isOpened()) return false;
    capture_.set(cv::CAP_PROP_AUTO_WB, 1);
    return capture_.get(cv::CAP_PROP_AUTO_WB) == 1.0;
}

cv::Size OpenCvCamera::frameSize() const {
    if (!capture_.isOpened()) return {};
    return {static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH)),
            static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT))};
}

std::string OpenCvCamera::pixelFormat() const {
    if (!capture_.isOpened()) return {};
    const auto code = static_cast<unsigned int>(static_cast<long long>(capture_.get(cv::CAP_PROP_FOURCC)));
    std::string fourcc;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
        if (c < 0x20 || c >= 0x7F) return {};
        fourcc += c;
    }
    return fourcc;
}

std::string OpenCvCamera::captureApiName() const {
    if (!capture_.isOpened()) return {};
    return capture_.getBackendName();
}

} // namespace msapp
