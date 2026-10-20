#include "camera_setup.hpp"
#include "opencv_camera.hpp"

#include <cmath>
#include <exception>

namespace msapp {

std::vector<std::string> platformCaptureApis() {
#if defined(_WIN32)
    return {"dshow", "msmf"};
#elif defined(__linux__)
    return {"v4l2"};
#else
    return {"auto"};
#endif
}

std::vector<FoundCamera> findCameras(const std::vector<std::string>& captureApis, int maxIndex,
                                     const CameraOpener& opener) {
    std::vector<FoundCamera> found;
    for (const std::string& api : captureApis) {
        for (int index = 0; index < maxIndex; ++index) {
            std::unique_ptr<ICamera> camera;
            try {
                camera = opener(index, api);
                if (!camera) continue;
                camera->open();
            } catch (const std::exception&) {
                continue;  // nothing there, or held by another program
            }
            FoundCamera one;
            one.deviceIndex = index;
            one.captureApi = api;
            one.defaultSize = camera->frameSize();
            one.defaultPixelFormat = camera->pixelFormat();
            // Some UVC cameras return empty frames while the stream starts up.
            for (int attempt = 0; attempt < 10 && !one.deliversFrames; ++attempt) {
                try {
                    one.deliversFrames = !camera->grab().empty();
                } catch (const std::exception&) {
                    one.deliversFrames = false;
                }
            }
            camera->close();
            found.push_back(one);
        }
    }
    return found;
}

std::vector<FoundCamera> findCameras(const std::vector<std::string>& captureApis, int maxIndex) {
    return findCameras(captureApis, maxIndex, [](int index, const std::string& api) -> std::unique_ptr<ICamera> {
        return std::make_unique<OpenCvCamera>(index, captureApiFromName(api));
    });
}

std::vector<CameraMode> commonCameraModes() {
    const CameraConfig a5 = moticamA5Profile();
    return {
        {a5.width, a5.height, a5.pixelFormat},
        {1920, 1080, "MJPG"},
        {1280, 720, "MJPG"},
        {1024, 768, ""},
        {0, 0, ""},
    };
}

std::vector<double> exposureStepsMs() {
    std::vector<double> steps;
    for (int power = 12; power >= 2; --power) steps.push_back(1000.0 / std::ldexp(1.0, power));
    return steps;
}

bool whiteReferenceHolds(const CameraConfig& measuredWith, const CameraConfig& now) {
    return measuredWith.backend == now.backend && measuredWith.deviceIndex == now.deviceIndex &&
           measuredWith.captureApi == now.captureApi && measuredWith.exposureMs == now.exposureMs &&
           measuredWith.whiteBalanceKelvin == now.whiteBalanceKelvin;
}

namespace {

bool sameGains(const WhiteBalance& a, const WhiteBalance& b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

} // namespace

CameraSetupReview reviewCameraSetup(const WorkstationConfig& current, const CameraConfig& proposed,
                                    const ProfileReport& granted) {
    CameraSetupReview review;
    review.profile = proposed;

    // The mode the camera delivers, not the one asked for (ADR 0015). Asking
    // for the camera's default (0 x 0, no FOURCC) is kept as such.
    const cv::Size requested(proposed.width, proposed.height);
    if (!granted.frameSize.empty() && !requested.empty() && granted.frameSize != requested) {
        review.profile.width = granted.frameSize.width;
        review.profile.height = granted.frameSize.height;
        review.frameSizeAdopted = true;
    }
    if (!granted.pixelFormat.empty() && !proposed.pixelFormat.empty() && granted.pixelFormat != proposed.pixelFormat) {
        review.profile.pixelFormat = granted.pixelFormat;
        review.pixelFormatAdopted = true;
    }
    review.exposureRefused = !granted.exposureApplied;
    review.whiteBalanceRefused = !granted.whiteBalanceApplied;

    review.frameSizeUnknown = granted.frameSize.empty();
    if (!review.frameSizeUnknown) {
        for (const Calibration& calibration : current.calibrations) {
            if (!calibration.fitsFrame(granted.frameSize.width, granted.frameSize.height)) {
                review.unusableCalibrations.push_back(calibration);
            }
        }
    }

    // Gains the wizard didn't measure again are the workstation's, measured
    // under its current profile.
    review.whiteReferenceMissing = proposed.whiteReference.isIdentity();
    review.whiteReferenceStale = !review.whiteReferenceMissing &&
                                 sameGains(proposed.whiteReference, current.camera.whiteReference) &&
                                 !whiteReferenceHolds(current.camera, proposed);

    review.changed = review.profile.toJson() != current.camera.toJson();
    return review;
}

} // namespace msapp
