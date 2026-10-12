#pragma once

#include "camera.hpp"

#include <opencv2/videoio.hpp>

namespace msapp {

// cv::VideoCapture backend. Only reaches UVC devices (DirectShow / Media
// Foundation on Windows, V4L2 on Linux). Motic USB cameras bind to WinUSB
// with a private interface and will not show up here — see ADR 0011.
class OpenCvCamera : public ICamera {
public:
    explicit OpenCvCamera(int deviceIndex, int apiPreference = cv::CAP_ANY);

    std::string backend() const override { return "opencv"; }
    void open() override;
    void close() override;
    bool isOpen() const override { return capture_.isOpened(); }
    cv::Mat grab() override;

    // Units are backend-specific in OpenCV (DirectShow takes log2 seconds),
    // so this converts ms for DirectShow/MSMF and passes ms through otherwise.
    bool setExposureMs(double milliseconds) override;
    bool setGain(double gain) override;

private:
    int deviceIndex_;
    int apiPreference_;
    cv::VideoCapture capture_;
};

} // namespace msapp
