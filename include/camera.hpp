#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace msapp {

// Per-channel gains; legacy ConfigurationMetal stored these per workstation
// (e.g. R 1.745, G 1.0, B 2.576 for the Basler it last drove).
struct WhiteBalance {
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
};

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
    virtual bool setGain(double /*gain*/) { return false; }
    virtual bool setWhiteBalance(const WhiteBalance& /*gains*/) { return false; }
};

} // namespace msapp
