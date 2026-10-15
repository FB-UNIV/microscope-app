#pragma once

#include "camera.hpp"

#include <opencv2/videoio.hpp>
#include <string>

namespace msapp {

// cv::VideoCapture backend for UVC cameras — the production path for the
// Moticam A5 (ADR 0013), via DirectShow / Media Foundation on Windows and
// V4L2 on Linux. WinUSB-only Motic models never show up here (ADR 0011).
class OpenCvCamera : public ICamera {
public:
    // requestedSize: frame size asked for at open (UVC cameras often start at
    // 640x480 otherwise); empty keeps the device default. The device may
    // pick the nearest mode it supports — read frameSize() after open().
    // pixelFormat: FOURCC such as "MJPG", requested before the size (the
    // Moticam A5 delivers its HD modes as MJPG); empty keeps the default.
    explicit OpenCvCamera(int deviceIndex, int apiPreference = cv::CAP_ANY, cv::Size requestedSize = {},
                          std::string pixelFormat = {});

    std::string backend() const override { return "opencv"; }
    void open() override;
    void close() override;
    bool isOpen() const override { return capture_.isOpened(); }
    cv::Mat grab() override;

    // Units are backend-specific in OpenCV (DirectShow takes log2 seconds),
    // so this converts ms for DirectShow/MSMF and passes ms through otherwise.
    bool setExposureMs(double milliseconds) override;
    // Best effort: OpenCV's auto-exposure values differ per backend and this
    // is not yet confirmed on the Moticam A5 (camera_probe --auto-exposure).
    bool enableAutoExposure() override;
    bool setGain(double gain) override;
    // Confirmed on the Moticam A5 through DirectShow (probe round 2).
    bool setWhiteBalanceKelvin(double kelvin) override;
    bool enableAutoWhiteBalance() override;

    // Actual negotiated frame size; empty while closed.
    cv::Size frameSize() const;
    // OpenCV's name for the capture API in use ("DSHOW", "MSMF", "V4L2"); empty while closed.
    std::string captureApiName() const;

private:
    int deviceIndex_;
    int apiPreference_;
    cv::Size requestedSize_;
    std::string pixelFormat_;
    cv::VideoCapture capture_;
};

} // namespace msapp
