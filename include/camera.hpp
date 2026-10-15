#pragma once

#include "white_balance.hpp"

#include <opencv2/core.hpp>
#include <string>

namespace msapp {

// Capture abstraction required by ADR 0001: everything above this interface
// (Vue::capture, UI, treatments) is backend-agnostic. Backends today:
//   "file"   — replays image files (no-camera mode, tests, demos)
//   "opencv" — cv::VideoCapture for UVC devices; the production path for
//              the lab's Moticam A5, which is UVC (ADR 0013)
//   "motic"  — reserved for a Motic SDK backend, only needed for WinUSB-only
//              Motic models (ADR 0011); not built
class ICamera {
public:
    virtual ~ICamera() = default;

    virtual std::string backend() const = 0;

    // Throws std::runtime_error with an actionable message if the device
    // can't be opened — a camera that silently yields nothing would look
    // like a broken UI rather than a missing device.
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // One frame, 8-bit BGR or grey. Throws std::runtime_error if not open or
    // the device returned nothing; never returns an empty Mat.
    virtual cv::Mat grab() = 0;

    // Optional controls. Return false when this backend/device can't apply
    // the setting, so the UI can grey the control out instead of lying.
    virtual bool setExposureMs(double /*milliseconds*/) { return false; }
    virtual bool enableAutoExposure() { return false; }
    virtual bool setGain(double /*gain*/) { return false; }
    // Hardware per-channel gains — UVC cameras don't offer them; the White
    // reference is applied in software by CaptureStation instead (ADR 0014).
    virtual bool setWhiteBalance(const WhiteBalance& /*gains*/) { return false; }
    // Locks the camera's colour temperature (turns auto white balance off).
    virtual bool setWhiteBalanceKelvin(double /*kelvin*/) { return false; }
    virtual bool enableAutoWhiteBalance() { return false; }
};

} // namespace msapp
