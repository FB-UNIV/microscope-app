#pragma once

#include "calibration.hpp"
#include "camera.hpp"
#include "camera_factory.hpp"
#include "vue.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace msapp {

// What the camera accepted when CaptureStation applied the profile on open.
// false means the device ignored the setting or the backend can't do it, so
// the UI can warn instead of silently capturing with unknown settings.
struct ProfileReport {
    bool exposureApplied = false;
    bool whiteBalanceApplied = false;
    // The mode the camera negotiated (ADR 0015); empty when the backend can't
    // tell. A driver that can't grant the profile's mode falls back silently,
    // and a Calibration measured in the profile's mode doesn't hold in another.
    cv::Size frameSize;
    std::string pixelFormat;
    // true when the profile requests nothing, or the camera granted exactly
    // what it requested; false on any other mode, or when it can't tell.
    bool frameSizeApplied = false;
    bool pixelFormatApplied = false;

    bool allApplied() const {
        return exposureApplied && whiteBalanceApplied && frameSizeApplied && pixelFormatApplied;
    }
};

// One workstation's capture setup (ADR 0014): a camera plus its profile,
// re-applied on every open — the Moticam A5 keeps the previous program's
// exposure and white balance otherwise — and the White reference baked
// into every preview frame and every capture.
class CaptureStation {
public:
    explicit CaptureStation(CameraConfig config);  // camera built with makeCamera(config)
    CaptureStation(std::unique_ptr<ICamera> camera, CameraConfig config);

    ProfileReport open();
    void close();
    bool isOpen() const;

    // White-balanced frame: exactly what a capture would store.
    cv::Mat grabPreview();

    // A frame for the live view, shrunk to fit within `fitWithin` before the
    // White reference is applied (whiteBalancedPreview): at 4K shown in
    // 1920x1080, 40 % less CPU per frame than shrinking grabPreview()
    // (docs/reviews/2026-10-19-performance-and-quality-review.md, §1.1).
    // Captures never go through here.
    //
    // A camera that hands over nothing for a moment costs a frame, not the
    // live view (review H2): UVC cameras send empty frames while the stream
    // starts up, and a USB hiccup drops one. A failed grab returns nullopt —
    // pause a little and call again — until kWarmUpGrabs in a row have
    // failed before the first frame since open(), or kDroppedGrabs in a row
    // after it: then it throws std::runtime_error. capture() and
    // calibrateWhiteReference() never retry: they fail loudly.
    struct LivePreview {
        cv::Mat image;
        cv::Size grabbedSize;  // the frame as grabbed: what a Calibration must match
    };
    std::optional<LivePreview> grabLivePreview(cv::Size fitWithin);

    // Grabs a frame and throws it away, keeping the stream moving while the
    // window has not shown the previous one yet. A failed grab counts as in
    // grabLivePreview(): false, or a throw once too many failed in a row.
    bool dropFrame();

    // Failed live grabs tolerated in a row, the last one throwing:
    // tools/camera_probe.cpp tries 10 times for the first frame too.
    static constexpr int kWarmUpGrabs = 10;
    static constexpr int kDroppedGrabs = 5;

    // Grabs a frame, bakes the White reference in, and saves it as a new
    // immutable Vue whose sidecar records how it was captured — the settings
    // the camera accepted at open(), not merely the ones the profile asked
    // for — and the Calibration of the Grossissement in use, if given. A
    // calibrated capture also gets an Échelle built from it (ADR 0017).
    // Throws std::runtime_error, writing nothing, when that Calibration was
    // measured at another frame size than the grabbed frame (ADR 0015).
    Vue capture(const std::filesystem::path& imagePath, const std::optional<Calibration>& calibration = std::nullopt);

    // Grabs one raw frame of a blank field (lamp on, no specimen) and stores
    // the gains that neutralise it. Persist config() afterwards to keep them
    // (WorkstationConfig::camera, saveWorkstationConfig).
    WhiteBalance calibrateWhiteReference();

    const CameraConfig& config() const { return config_; }
    // What the last open() reported; all false before the first open().
    const ProfileReport& profileReport() const { return report_; }

private:
    std::unique_ptr<ICamera> camera_;
    CameraConfig config_;
    ProfileReport report_;
    bool liveFrameSeen_ = false;  // since the last open()
    int failedLiveGrabs_ = 0;     // in a row

    // camera_->grab(), an empty frame refused with std::runtime_error.
    cv::Mat grabFrame();
    // grabFrame() for the live view: nullopt while failures are tolerated.
    std::optional<cv::Mat> grabLiveFrame();
};

} // namespace msapp
