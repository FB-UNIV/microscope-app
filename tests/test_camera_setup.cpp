// User journey:
//   As a teacher setting up a new lab PC, I configure its camera from the
//   app instead of editing workstation.json (issue #83): the app lists the
//   cameras plugged in, offers the Moticam A5's modes and exposures, shows
//   what the camera actually granted, and before saving tells me which
//   Calibrations stop applying in the new mode (ADR 0015) and whether the
//   White reference must be measured again (ADR 0014).

#include "camera_setup.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace msapp;
using namespace msapp::test;

namespace {

// A camera at one index: opens or throws, reports a default mode, delivers
// frames after `emptyFrames` empty grabs (or never, with -1).
class FakeCamera : public ICamera {
public:
    FakeCamera(cv::Size size, std::string format, int emptyFrames, int* closes)
        : size_(size), format_(std::move(format)), emptyFrames_(emptyFrames), closes_(closes) {}

    std::string backend() const override { return "fake"; }
    void open() override { open_ = true; }
    void close() override {
        open_ = false;
        ++*closes_;
    }
    bool isOpen() const override { return open_; }
    cv::Mat grab() override {
        if (emptyFrames_ < 0) throw std::runtime_error("FakeCamera: no frame");
        if (emptyFrames_ > 0) {
            --emptyFrames_;
            throw std::runtime_error("FakeCamera: stream starting");
        }
        return cv::Mat(size_.empty() ? cv::Size(4, 3) : size_, CV_8UC3, cv::Scalar(1, 2, 3));
    }
    cv::Size frameSize() const override { return open_ ? size_ : cv::Size(); }
    std::string pixelFormat() const override { return open_ ? format_ : std::string(); }

private:
    cv::Size size_;
    std::string format_;
    int emptyFrames_;
    int* closes_;
    bool open_ = false;
};

class Unpluggable : public ICamera {
public:
    std::string backend() const override { return "fake"; }
    void open() override { throw std::runtime_error("no camera here"); }
    void close() override {}
    bool isOpen() const override { return false; }
    cv::Mat grab() override { throw std::runtime_error("not open"); }
};

Calibration measured(const std::string& grossissement, double ppm, int width, int height) {
    Calibration c;
    c.grossissement = grossissement;
    c.pixelsPerMicron = ppm;
    c.frameWidth = width;
    c.frameHeight = height;
    return c;
}

// What the camera reports when it grants everything the profile asks for.
ProfileReport grantedAll(const CameraConfig& profile) {
    ProfileReport r;
    r.exposureApplied = true;
    r.whiteBalanceApplied = true;
    r.frameSize = cv::Size(profile.width, profile.height);
    r.pixelFormat = profile.pixelFormat;
    r.frameSizeApplied = true;
    r.pixelFormatApplied = true;
    return r;
}

// A lab PC set up on the A5 profile, with a White reference and x5
// calibrated at 4K.
WorkstationConfig labWorkstation() {
    WorkstationConfig w;
    w.camera = moticamA5Profile();
    w.camera.whiteReference = {1.745, 1.0, 2.576};
    w.setCalibration(measured("x5", 1.55, 3840, 2160));
    return w;
}

} // namespace

int main() {
    std::cout << "== findCameras(): what is plugged in, like camera_probe ==\n";
    {
        int closes = 0;
        std::vector<std::string> tried;
        const auto opener = [&](int index, const std::string& api) -> std::unique_ptr<ICamera> {
            tried.push_back(api + ":" + std::to_string(index));
            if (api == "dshow" && index == 0) {
                return std::make_unique<FakeCamera>(cv::Size(1024, 768), "", 2, &closes);  // the A5, slow to start
            }
            if (api == "dshow" && index == 2) {
                return std::make_unique<FakeCamera>(cv::Size(640, 480), "YUY2", -1, &closes);  // opens, no frame
            }
            if (api == "msmf" && index == 0) return std::make_unique<FakeCamera>(cv::Size(), "", 0, &closes);
            if (index == 1) throw std::invalid_argument("bad index");  // a throwing opener is skipped too
            return std::make_unique<Unpluggable>();
        };
        const std::vector<FoundCamera> found = findCameras({"dshow", "msmf"}, 3, opener);
        check(tried == std::vector<std::string>{"dshow:0", "dshow:1", "dshow:2", "msmf:0", "msmf:1", "msmf:2"},
              "every index is tried through each capture API, in the order given");
        check(found.size() == 3, "only the indexes that opened are listed");
        check(found.size() == 3 && found[0].deviceIndex == 0 && found[0].captureApi == "dshow" &&
                  found[0].defaultSize == cv::Size(1024, 768) && found[0].defaultPixelFormat.empty() &&
                  found[0].deliversFrames,
              "a camera lists its index, capture API and default mode; empty frames at start are allowed");
        check(found.size() == 3 && found[1].deviceIndex == 2 && found[1].defaultPixelFormat == "YUY2" &&
                  !found[1].deliversFrames,
              "a camera that opens but never delivers a frame is listed as such");
        check(found.size() == 3 && found[2].captureApi == "msmf" && found[2].defaultSize.empty(),
              "a camera that can't tell its mode is listed with an empty one");
        check(closes == 3, "every camera opened is closed again, for the preview to reopen it");
        check(findCameras({}, 4, opener).empty() && findCameras({"dshow"}, 0, opener).empty(),
              "no capture API or no index: nothing is tried");
    }
    {
        const auto none = [](int, const std::string&) -> std::unique_ptr<ICamera> { return nullptr; };
        check(findCameras({"dshow"}, 2, none).empty(), "an opener with nothing to offer finds nothing");
    }

    std::cout << "\n== What the wizard offers ==\n";
    {
        const std::vector<std::string> apis = platformCaptureApis();
        check(!apis.empty(), "at least one capture API to try on this platform");
        for (const std::string& api : apis) {
            check(throwsWith<std::invalid_argument>([&] { captureApiFromName(api); }).empty(),
                  "capture API '" + api + "' is one the camera profile accepts");
        }
#if defined(_WIN32)
        check(apis.front() == "dshow", "DirectShow first on Windows: only it exposes white balance on the A5");
#endif

        const std::vector<CameraMode> modes = commonCameraModes();
        const CameraConfig a5 = moticamA5Profile();
        check(!modes.empty() && modes.front().width == a5.width && modes.front().height == a5.height &&
                  modes.front().pixelFormat == a5.pixelFormat,
              "the A5 profile's mode, 3840x2160 MJPG, comes first");
        check(std::any_of(modes.begin(), modes.end(), [](const CameraMode& m) { return m.width == 0 && m.height == 0; }),
              "the camera's default mode is offered");

        const std::vector<double> steps = exposureStepsMs();
        check(std::is_sorted(steps.begin(), steps.end()) && std::adjacent_find(steps.begin(), steps.end()) == steps.end(),
              "exposure steps go from shortest to longest");
        check(a5.exposureMs && std::find(steps.begin(), steps.end(), *a5.exposureMs) != steps.end(),
              "the A5 profile's 7.8125 ms is one of the steps exactly");
        check(steps.size() >= 2 && nearlyEqual(steps[1] / steps[0], 2.0), "steps are whole powers of two of a second");
    }

    std::cout << "\n== reviewCameraSetup(): nothing changed ==\n";
    {
        const WorkstationConfig w = labWorkstation();
        const CameraSetupReview review = reviewCameraSetup(w, w.camera, grantedAll(w.camera));
        check(!review.changed, "the same profile, all granted: nothing changes");
        check(!review.calibrationsLost() && !review.whiteReferenceStale && !review.whiteReferenceMissing,
              "nothing to warn about");
        check(!review.frameSizeAdopted && !review.pixelFormatAdopted && !review.exposureRefused &&
                  !review.whiteBalanceRefused && !review.frameSizeUnknown,
              "nothing refused or adopted");
        check(review.profile.toJson() == w.camera.toJson(), "the profile to save is the proposal");
    }

    std::cout << "\n== reviewCameraSetup(): a new mode (ADR 0015) ==\n";
    {
        WorkstationConfig w = labWorkstation();
        w.setCalibration(measured("x50", 15.5, 3840, 2160));
        CameraConfig proposed = w.camera;
        proposed.width = 1920;
        proposed.height = 1080;
        const CameraSetupReview review = reviewCameraSetup(w, proposed, grantedAll(proposed));
        check(review.changed, "another frame size is a change");
        check(review.unusableCalibrations.size() == 2 && review.unusableCalibrations[0].grossissement == "x5" &&
                  review.unusableCalibrations[1].grossissement == "x50",
              "every mire Calibration measured at 3840x2160 is named: it won't apply at 1920x1080");
        check(!review.whiteReferenceStale, "the frame size alone doesn't make the White reference stale");
    }
    {
        // Asked for 4K, the camera fell back to its 1024x768 default.
        const WorkstationConfig w = labWorkstation();
        CameraConfig proposed = w.camera;
        ProfileReport granted = grantedAll(proposed);
        granted.frameSize = cv::Size(1024, 768);
        granted.frameSizeApplied = false;
        granted.pixelFormat = "YUY2";
        granted.pixelFormatApplied = false;
        const CameraSetupReview review = reviewCameraSetup(w, proposed, granted);
        check(review.frameSizeAdopted && review.profile.width == 1024 && review.profile.height == 768,
              "the profile saved asks for the frame size the camera delivered, not the one refused");
        check(review.pixelFormatAdopted && review.profile.pixelFormat == "YUY2", "... and its pixel format");
        check(review.changed, "which is a change from the saved profile");
        check(review.calibrationsLost(), "the 4K Calibration is named: it won't apply at 1024x768");
    }
    {
        // Media Foundation often reports no FOURCC, and a camera may not tell its size.
        const WorkstationConfig w = labWorkstation();
        CameraConfig proposed = w.camera;
        ProfileReport granted = grantedAll(proposed);
        granted.pixelFormat.clear();
        granted.pixelFormatApplied = false;
        granted.frameSize = cv::Size();
        granted.frameSizeApplied = false;
        const CameraSetupReview review = reviewCameraSetup(w, proposed, granted);
        check(!review.pixelFormatAdopted && review.profile.pixelFormat == "MJPG",
              "an unknown pixel format keeps the one asked for");
        check(!review.frameSizeAdopted && review.profile.width == 3840 && review.frameSizeUnknown,
              "an unknown frame size keeps the one asked for, and says the Calibrations can't be checked");
        check(!review.calibrationsLost(), "no Calibration is named unusable on a size nobody knows");
    }
    {
        // The camera's default mode, asked for as 0x0, is kept as such.
        const WorkstationConfig w = labWorkstation();
        CameraConfig proposed = w.camera;
        proposed.width = 0;
        proposed.height = 0;
        proposed.pixelFormat.clear();
        ProfileReport granted = grantedAll(w.camera);
        granted.frameSize = cv::Size(1024, 768);
        granted.pixelFormat = "YUY2";
        const CameraSetupReview review = reviewCameraSetup(w, proposed, granted);
        check(!review.frameSizeAdopted && !review.pixelFormatAdopted && review.profile.width == 0 &&
                  review.profile.pixelFormat.empty(),
              "asking for the camera's default mode stays a request for its default");
        check(review.calibrationsLost(), "Calibrations are still checked against the size it delivers");
    }
    {
        // A Calibration stored without a frame size is accepted unverified (ADR 0015).
        WorkstationConfig w;
        w.camera = moticamA5Profile();
        w.calibrations.push_back(measured("x10", 3.1, 0, 0));
        CameraConfig proposed = w.camera;
        proposed.width = 1920;
        proposed.height = 1080;
        check(!reviewCameraSetup(w, proposed, grantedAll(proposed)).calibrationsLost(),
              "a Calibration without a frame size is not named unusable");
    }

    std::cout << "\n== reviewCameraSetup(): the White reference (ADR 0014) ==\n";
    {
        const WorkstationConfig w = labWorkstation();
        CameraConfig proposed = w.camera;
        proposed.exposureMs = 3.90625;
        check(reviewCameraSetup(w, proposed, grantedAll(proposed)).whiteReferenceStale,
              "a new exposure makes the White reference stale");
        proposed = w.camera;
        proposed.whiteBalanceKelvin = 4500.0;
        check(reviewCameraSetup(w, proposed, grantedAll(proposed)).whiteReferenceStale,
              "a new colour temperature makes it stale");
        proposed = w.camera;
        proposed.whiteBalanceKelvin.reset();
        check(reviewCameraSetup(w, proposed, grantedAll(proposed)).whiteReferenceStale,
              "going to auto white balance makes it stale");
        proposed = w.camera;
        proposed.deviceIndex = 1;
        check(reviewCameraSetup(w, proposed, grantedAll(proposed)).whiteReferenceStale,
              "another camera makes it stale");
        proposed = w.camera;
        proposed.captureApi = "msmf";
        check(reviewCameraSetup(w, proposed, grantedAll(proposed)).whiteReferenceStale,
              "another capture API makes it stale");

        proposed = w.camera;
        proposed.exposureMs = 3.90625;
        proposed.whiteReference = {1.6, 1.0, 2.4};  // measured again in the wizard
        const CameraSetupReview remeasured = reviewCameraSetup(w, proposed, grantedAll(proposed));
        check(!remeasured.whiteReferenceStale && !remeasured.whiteReferenceMissing && remeasured.changed,
              "a White reference measured again with the new settings is not stale");
    }
    {
        WorkstationConfig w = labWorkstation();
        w.camera.whiteReference = WhiteBalance();
        CameraConfig proposed = w.camera;
        proposed.exposureMs = 3.90625;
        const CameraSetupReview review = reviewCameraSetup(w, proposed, grantedAll(proposed));
        check(review.whiteReferenceMissing && !review.whiteReferenceStale,
              "with no White reference measured yet, it is missing rather than stale");
    }
    {
        CameraConfig a = moticamA5Profile();
        CameraConfig b = a;
        b.width = 1920;
        b.height = 1080;
        b.pixelFormat = "YUY2";
        b.whiteReference = {2.0, 1.0, 2.0};
        check(whiteReferenceHolds(a, b), "frame size, pixel format and the gains themselves don't matter");
        b = a;
        b.backend = "file";
        check(!whiteReferenceHolds(a, b), "another backend does");
    }

    std::cout << "\n== reviewCameraSetup(): settings the camera refused ==\n";
    {
        const WorkstationConfig w = labWorkstation();
        CameraConfig proposed = w.camera;
        proposed.exposureMs = 15.625;
        ProfileReport granted = grantedAll(proposed);
        granted.exposureApplied = false;
        granted.whiteBalanceApplied = false;
        const CameraSetupReview review = reviewCameraSetup(w, proposed, granted);
        check(review.exposureRefused && review.whiteBalanceRefused, "refused exposure and colour temperature are named");
        check(review.profile.exposureMs == 15.625 && review.profile.whiteBalanceKelvin == 5000.0,
              "the profile keeps asking for them: re-applied at every open, the banner names each refusal");
    }

    return summary();
}
