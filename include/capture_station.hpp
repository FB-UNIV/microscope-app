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

    // Applies a new exposure and colour temperature to the open camera
    // (empty = the camera's auto), as open() does, without reopening it:
    // the camera setup wizard shows each setting's effect live. Returns the
    // updated profileReport(); the mode is left as negotiated at open().
    // Throws std::runtime_error when the camera is not open.
    ProfileReport setControls(std::optional<double> exposureMs, std::optional<double> whiteBalanceKelvin);

    // White-balanced frame: exactly what a capture would store.
    cv::Mat grabPreview();

    // A frame for the live view, shrunk to fit within `fitWithin` before the
    // White reference is applied (whiteBalancedPreview): at 4K shown in
    // 1920x1080, 40 % less CPU per frame than shrinking grabPreview()
    // (docs/reviews/2026-10-19-performance-and-quality-review.md, §1.1).
    // Captures never go through here.
    struct LivePreview {
        cv::Mat image;
        cv::Size grabbedSize;  // the frame as grabbed: what a Calibration must match
    };
    LivePreview grabLivePreview(cv::Size fitWithin);

    // Grabs a frame and throws it away, keeping the stream moving while the
    // window has not shown the previous one yet.
    void dropFrame();

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
    // Sets both controls from config_, even "auto", into report_.
    void applyControls();

    std::unique_ptr<ICamera> camera_;
    CameraConfig config_;
    ProfileReport report_;
};

} // namespace msapp
