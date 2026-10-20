#pragma once

#include "calibration.hpp"
#include "camera.hpp"
#include "camera_factory.hpp"
#include "capture_station.hpp"
#include "workstation_config.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace msapp {

// The logic behind the camera setup wizard (Poste › Configurer la caméra,
// issue #83), kept out of the Qt app so it is tested here: which cameras
// are plugged in, which modes and exposures to offer, and what saving a
// new Camera profile means for the White reference (ADR 0014) and the
// Calibrations (ADR 0015).

// One camera findCameras() opened, as it comes before any profile is
// applied — what camera_probe prints for each index.
struct FoundCamera {
    int deviceIndex = 0;
    std::string captureApi;          // the CameraConfig::captureApi name it opened through, e.g. "dshow"
    cv::Size defaultSize;            // its default mode; empty when it can't tell
    std::string defaultPixelFormat;  // FOURCC; empty when it can't tell
    bool deliversFrames = false;     // a frame came back, not merely an open device
};

// The capture APIs worth trying on this platform, best first: "dshow" (the
// only one exposing white balance on the Moticam A5) then "msmf" on
// Windows, "v4l2" on Linux, "auto" elsewhere.
std::vector<std::string> platformCaptureApis();

// Builds the camera to try at one index through one capture API.
using CameraOpener = std::function<std::unique_ptr<ICamera>(int deviceIndex, const std::string& captureApi)>;

// Tries indexes 0 to maxIndex - 1 through each of `captureApis`, in that
// order, and lists every camera that opened, closed again afterwards. An
// index that throws on open is skipped: nothing is plugged in there, or
// another program holds it. A few empty frames are allowed while the
// stream starts, as camera_probe does. The camera in use must be closed
// first: a UVC camera opens in one place at a time.
std::vector<FoundCamera> findCameras(const std::vector<std::string>& captureApis, int maxIndex,
                                     const CameraOpener& opener);
// The same through OpenCvCamera, in each camera's default mode.
std::vector<FoundCamera> findCameras(const std::vector<std::string>& captureApis, int maxIndex = 4);

// A frame size and pixel format to request.
struct CameraMode {
    int width = 0;  // 0 x 0: the camera's default mode
    int height = 0;
    std::string pixelFormat;  // FOURCC; empty: the camera's default
};

// The modes the wizard offers, the Moticam A5 profile's first: 3840x2160
// and 1920x1080 MJPG (the A5's HD modes come as MJPG), 1280x720 MJPG,
// 1024x768 (the A5's default), and the camera's default mode.
std::vector<CameraMode> commonCameraModes();

// The exposures the wizard offers, in ms, shortest first: 2^-12 to 2^-2 s.
// DirectShow and Media Foundation take exposure as whole powers of two of
// a second, so the steps in between don't exist there; 7.8125 ms (2^-7 s)
// is the A5 profile's.
std::vector<double> exposureStepsMs();

// What saving `proposed` over the workstation's Camera profile would mean,
// given what the camera granted when it was opened with it. Nothing here
// is saved as granted when the camera refused it.
struct CameraSetupReview {
    // What to save: `proposed`, but asking for the mode the camera
    // actually delivered when it fell back to another one, so that the
    // next open doesn't warn and the Calibrations measured from now on are
    // checked against the right size. Exposure and colour temperature stay
    // as asked even when refused: the profile is re-applied on every open,
    // and the banner names each setting refused there.
    CameraConfig profile;
    bool frameSizeAdopted = false;    // the camera delivered another frame size than asked
    bool pixelFormatAdopted = false;  // ... another pixel format
    bool exposureRefused = false;
    bool whiteBalanceRefused = false;
    // The camera didn't say its frame size: the Calibrations can't be checked.
    bool frameSizeUnknown = false;
    // The mire Calibrations measured at another frame size than the camera
    // now delivers: they no longer apply (ADR 0015), and neither do the
    // Grossissements derived from them (ADR 0020). Recalibrating one
    // objective on the mire is enough.
    std::vector<Calibration> unusableCalibrations;
    // The White reference was measured with another camera, exposure or
    // colour temperature than the new profile's: measure it again on a
    // blank field (ADR 0014). Not set when the proposal carries a newly
    // measured one.
    bool whiteReferenceStale = false;
    // No White reference was ever measured on this microscope (identity gains).
    bool whiteReferenceMissing = false;
    // The profile to save differs from the workstation's.
    bool changed = false;

    bool calibrationsLost() const { return !unusableCalibrations.empty(); }
};

// `current` is the workstation as saved; `granted` what the camera
// reported when opened with `proposed` (CaptureStation::open, then any
// CaptureStation::setControls).
CameraSetupReview reviewCameraSetup(const WorkstationConfig& current, const CameraConfig& proposed,
                                    const ProfileReport& granted);

// True when a White reference measured under `measuredWith` still holds
// under `now`: same camera (backend, index, capture API), same exposure
// and same colour temperature (ADR 0014). The frame size doesn't matter.
bool whiteReferenceHolds(const CameraConfig& measuredWith, const CameraConfig& now);

} // namespace msapp
