#pragma once

#include "camera.hpp"
#include "camera_factory.hpp"
#include "vue.hpp"

#include <filesystem>
#include <memory>

namespace msapp {

// What the camera accepted when CaptureStation applied the profile on open.
// false means the device ignored the setting or the backend can't do it, so
// the UI can warn instead of silently capturing with unknown settings.
struct ProfileReport {
    bool exposureApplied = false;
    bool whiteBalanceApplied = false;
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

    // Grabs a frame, bakes the White reference in, and saves it as a new
    // immutable Vue whose sidecar records how it was captured.
    Vue capture(const std::filesystem::path& imagePath);

    // Grabs one raw frame of a blank field (lamp on, no specimen) and stores
    // the gains that neutralise it. Persist config() afterwards to keep them.
    WhiteBalance calibrateWhiteReference();

    const CameraConfig& config() const { return config_; }

private:
    std::unique_ptr<ICamera> camera_;
    CameraConfig config_;
};

} // namespace msapp
