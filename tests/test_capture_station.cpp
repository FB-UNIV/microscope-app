// User journey:
//   As a student at a lab workstation, every photo I capture comes out with
//   the same exposure and colour as my neighbour's — the camera profile is
//   re-applied whenever the camera opens (the Moticam A5 keeps the last
//   program's settings otherwise), the microscope's White reference is baked
//   in, and the photo's sidecar records exactly how it was taken (ADR 0014).

#include "capture_station.hpp"
#include "echelle_treatment.hpp"
#include "levels_treatment.hpp"
#include "measurement.hpp"
#include "vue.hpp"
#include "test_support.hpp"

#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// Records which controls the station applied; frames are a fixed tinted
// field, and the frame's size and `pixelFormat` are the mode it "negotiated"
// — whatever the profile asked for, like a driver falling back silently.
class FakeCamera : public ICamera {
public:
    FakeCamera(cv::Mat frame, bool acceptsControls, std::string pixelFormat = {})
        : frame_(std::move(frame)), accepts_(acceptsControls), format_(std::move(pixelFormat)) {}

    std::string backend() const override { return "fake"; }
    void open() override { open_ = true; }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    cv::Mat grab() override {
        if (!open_) throw std::runtime_error("FakeCamera: not open");
        return frame_.clone();
    }
    cv::Size frameSize() const override { return open_ ? frame_.size() : cv::Size(); }
    std::string pixelFormat() const override { return open_ ? format_ : std::string(); }
    bool setExposureMs(double ms) override { return record("exposure " + std::to_string(static_cast<int>(ms))); }
    bool enableAutoExposure() override { return record("auto_exposure"); }
    bool setWhiteBalanceKelvin(double k) override { return record("kelvin " + std::to_string(static_cast<int>(k))); }
    bool enableAutoWhiteBalance() override { return record("auto_wb"); }

    std::vector<std::string> calls;

private:
    bool record(const std::string& call) {
        calls.push_back(call);
        return accepts_;
    }
    cv::Mat frame_;
    bool accepts_;
    std::string format_;
    bool open_ = false;
};

const cv::Scalar kTint(200, 120, 180);  // green-deficient, like the A5

CameraConfig lockedProfile() {
    CameraConfig c;
    c.backend = "fake";
    c.exposureMs = 20.0;
    c.whiteBalanceKelvin = 5000.0;
    return c;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_capture_station_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::cout << "== open(): profile applied on every open ==\n";
    {
        auto fake = std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true);
        FakeCamera* cam = fake.get();
        CaptureStation station(std::move(fake), lockedProfile());
        ProfileReport report = station.open();
        check(report.exposureApplied && report.whiteBalanceApplied, "report says both settings were accepted");
        check(cam->calls == std::vector<std::string>{"exposure 20", "kelvin 5000"},
              "configured exposure and colour temperature are set");
        station.close();
        station.open();
        check(cam->calls.size() == 4 && cam->calls[2] == "exposure 20" && cam->calls[3] == "kelvin 5000",
              "re-opening re-applies them (the camera keeps the last program's settings)");
    }
    {
        auto fake = std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true);
        FakeCamera* cam = fake.get();
        CameraConfig autoProfile;
        autoProfile.backend = "fake";
        CaptureStation station(std::move(fake), autoProfile);
        station.open();
        check(cam->calls == std::vector<std::string>{"auto_exposure", "auto_wb"},
              "an unset profile actively restores auto, not 'whatever was left'");
    }
    {
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), false), lockedProfile());
        ProfileReport report = station.open();
        check(!report.exposureApplied && !report.whiteBalanceApplied,
              "a device that refuses settings is reported, not hidden");
    }

    std::cout << "\n== open(): the negotiated mode is checked against the profile (ADR 0015) ==\n";
    {
        CameraConfig profile = lockedProfile();
        profile.width = 80;
        profile.height = 60;
        profile.pixelFormat = "MJPG";
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true, "MJPG"), profile);
        ProfileReport report = station.open();
        check(report.frameSize == cv::Size(80, 60) && report.pixelFormat == "MJPG",
              "report gives the size and pixel format the camera negotiated");
        check(report.frameSizeApplied && report.pixelFormatApplied && report.allApplied(),
              "the requested mode was granted");
    }
    {
        // Like the A5 asked for 3840x2160 MJPG and falling back to its default mode.
        CameraConfig profile = lockedProfile();
        profile.width = 160;
        profile.height = 120;
        profile.pixelFormat = "MJPG";
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true, "YUY2"), profile);
        ProfileReport report = station.open();
        check(!report.frameSizeApplied && report.frameSize == cv::Size(80, 60),
              "a fallback frame size is flagged, with the size actually in use");
        check(!report.pixelFormatApplied && report.pixelFormat == "YUY2", "a fallback pixel format is flagged");
        check(!report.allApplied(), "allApplied() is false, so the UI warns before anyone captures");
        check(!station.profileReport().frameSizeApplied, "the station keeps the last report");
    }
    {
        CameraConfig profile = lockedProfile();
        profile.pixelFormat = "MJPG";
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true), profile);
        ProfileReport report = station.open();
        check(report.frameSizeApplied, "no size requested: whatever the camera picked is fine");
        check(!report.pixelFormatApplied, "a camera that can't tell its pixel format is not assumed to comply");
    }

    std::cout << "\n== White reference: calibrate, then bake into preview and capture ==\n";
    {
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true), lockedProfile());
        station.open();
        cv::Mat before = station.grabPreview();
        check(before.at<cv::Vec3b>(5, 5) == cv::Vec3b(200, 120, 180), "uncalibrated preview is the raw frame");

        WhiteBalance gains = station.calibrateWhiteReference();
        check(nearlyEqual(gains.red, 120.0 / 180.0) && nearlyEqual(gains.blue, 120.0 / 200.0),
              "calibration measures the blank field's gains");
        check(nearlyEqual(station.config().whiteReference.blue, gains.blue),
              "gains are kept in config() so the workstation can persist them");

        const cv::Vec3b px = station.grabPreview().at<cv::Vec3b>(5, 5);
        check(std::abs(px[0] - 120) <= 1 && std::abs(px[1] - 120) <= 1 && std::abs(px[2] - 120) <= 1,
              "preview after calibration is neutral");

        const fs::path imagePath = dir / "vue_a5.png";
        Vue vue = station.capture(imagePath);
        cv::Mat onDisk = cv::imread(imagePath.string(), cv::IMREAD_UNCHANGED);
        const cv::Vec3b stored = onDisk.at<cv::Vec3b>(5, 5);
        check(std::abs(stored[0] - 120) <= 1 && std::abs(stored[2] - 120) <= 1,
              "the saved original has the White reference baked in");
        check(fs::exists(vue.sidecarPath()), "sidecar written at capture time, before any Treatment");

        Vue reloaded = Vue::load(imagePath);
        const auto& info = reloaded.captureInfo();
        check(info.has_value(), "reloaded Vue knows how it was captured");
        if (info) {
            check(info->cameraBackend == "fake" && info->exposureMs && nearlyEqual(*info->exposureMs, 20.0) &&
                      info->whiteBalanceKelvin && nearlyEqual(*info->whiteBalanceKelvin, 5000.0),
                  "capture info records backend, exposure and colour temperature");
            check(nearlyEqual(info->whiteReference.blue, gains.blue) && nearlyEqual(info->whiteReference.red, gains.red),
                  "capture info records the gains that were baked in");
            check(info->frameWidth == 80 && info->frameHeight == 60, "capture info records the frame size");
        }
        check(!reloaded.calibration().has_value(), "captured without a Calibration: none is invented");

        bool threw = false;
        try {
            station.capture(imagePath);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "capturing onto an existing path still refuses (Vues are immutable-by-path)");
    }

    std::cout << "\n== capture(): the sidecar records what the camera applied, not what was asked ==\n";
    {
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), false, "MJPG"),
                               lockedProfile());
        station.open();
        const fs::path imagePath = dir / "vue_refused.png";
        station.capture(imagePath);
        const auto info = Vue::load(imagePath).captureInfo();
        check(info && !info->exposureMs && !info->whiteBalanceKelvin,
              "exposure and colour temperature the camera refused are null, not the profile's values");
        check(info && info->frameWidth == 80 && info->frameHeight == 60 && info->pixelFormat == "MJPG",
              "frame size and negotiated pixel format are recorded");
    }

    std::cout << "\n== capture(): the Vue keeps its Calibration, checked against the frame (ADR 0015) ==\n";
    {
        CaptureStation station(std::make_unique<FakeCamera>(cv::Mat(60, 80, CV_8UC3, kTint), true), lockedProfile());
        station.open();

        const fs::path imagePath = dir / "vue_calibrated.png";
        station.capture(imagePath, Calibration{"x40", 2.5, 80, 60});
        Vue reloaded = Vue::load(imagePath);
        const auto& cal = reloaded.calibration();
        check(cal && cal->grossissement == "x40" && nearlyEqual(cal->pixelsPerMicron, 2.5) && cal->frameWidth == 80 &&
                  cal->frameHeight == 60,
              "the Calibration survives a reload, frame size included");
        Measurement m({0.0, 0.0}, {25.0, 0.0}, reloaded);
        check(nearlyEqual(m.realDistanceMicrons(), 10.0), "a Measurement on the reloaded Vue uses its Calibration");
        check(reloaded.treatmentCount() == 1, "a calibrated capture gets its Échelle straight away (ADR 0017)");
        std::ifstream sidecarIn(reloaded.sidecarPath());
        const nlohmann::json sidecar = nlohmann::json::parse(sidecarIn);
        check(sidecar.at("treatments").at(0).at("type") == "echelle" &&
                  sidecar.at("treatments").at(0).at("params").at("calibration") == sidecar.at("calibration"),
              "that Échelle is built from the Vue's own Calibration");

        // Calibrated at the profile's mode, but the camera fell back to another one.
        const fs::path mismatchPath = dir / "vue_mismatch.png";
        std::string message;
        try {
            station.capture(mismatchPath, Calibration{"x40", 2.5, 160, 120});
        } catch (const std::runtime_error& e) {
            message = e.what();
        }
        check(message.find("160x120") != std::string::npos && message.find("80x60") != std::string::npos,
              "a Calibration measured at another frame size is refused, naming both sizes");
        check(!fs::exists(mismatchPath) && !fs::exists(mismatchPath.string() + ".json"),
              "nothing is written for a refused capture");

        const fs::path uncalibratedPath = dir / "vue_uncalibrated.png";
        Vue uncalibrated = station.capture(uncalibratedPath);
        check(uncalibrated.treatmentCount() == 0, "an uncalibrated capture gets no Échelle");
        bool threw = false;
        try {
            Measurement({0.0, 0.0}, {25.0, 0.0}, uncalibrated);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "measuring on a Vue without a Calibration is an error, not a silent guess");
    }

    std::cout << "\n== Sidecars written before ADR 0015 still load ==\n";
    {
        // ADR 0014 object sidecar: "capture" without frame size, no "calibration".
        const fs::path imagePath = dir / "adr0014_vue.png";
        cv::imwrite(imagePath.string(), cv::Mat(4, 4, CV_8UC3, kTint));
        const nlohmann::json capture = {{"camera_backend", "opencv"},
                                        {"exposure_ms", 20.0},
                                        {"white_balance_kelvin", 5000.0},
                                        {"white_reference", WhiteBalance{}.toJson()}};
        std::ofstream(imagePath.string() + ".json")
            << nlohmann::json{{"capture", capture}, {"treatments", nlohmann::json::array()}}.dump();
        Vue old = Vue::load(imagePath);
        check(old.captureInfo() && old.captureInfo()->cameraBackend == "opencv" && old.captureInfo()->exposureMs &&
                  old.captureInfo()->frameWidth == 0 && old.captureInfo()->pixelFormat.empty(),
              "ADR 0014 sidecar: capture info loads, frame size unknown");
        check(!old.calibration().has_value(), "ADR 0014 sidecar: an uncalibrated Vue, not an error");
    }

    std::cout << "\n== Load: a Calibration or Échelle measured at another frame size costs only itself ==\n";
    {
        // A 4x4 photo whose sidecar Calibration and Échelle say 3840x2160:
        // they no longer describe it, but the photo, its other treatments and
        // its capture info do (ADR 0003 over ADR 0015).
        const fs::path imagePath = dir / "mismatched_vue.png";
        cv::imwrite(imagePath.string(), cv::Mat(4, 4, CV_8UC3, kTint));
        const nlohmann::json at4k = Calibration{"x40", 2.5, 3840, 2160}.toJson();
        const nlohmann::json treatments =
            nlohmann::json::array({{{"type", "levels"}, {"params", {{"black_point", 90}, {"white_point", 180}}}},
                                   {{"type", "echelle"}, {"params", {{"calibration", at4k}}}}});
        const nlohmann::json capture = {{"camera_backend", "opencv"}, {"exposure_ms", 20.0}};
        const fs::path sidecar = imagePath.string() + ".json";
        const std::string onDisk =
            nlohmann::json{{"capture", capture}, {"calibration", at4k}, {"treatments", treatments}}.dump();
        std::ofstream(sidecar) << onDisk;

        Vue vue = Vue::load(imagePath);
        check(vue.original().size() == cv::Size(4, 4), "the photo loads, no exception");
        check(!vue.calibration().has_value(), "the mismatched Calibration is dropped");
        check(vue.captureInfo() && vue.captureInfo()->exposureMs && nearlyEqual(*vue.captureInfo()->exposureMs, 20.0),
              "the capture info is kept");
        check(vue.treatmentCount() == 1, "the Levels treatment is kept; only the mismatched Échelle is dropped");
        check(!vue.render().empty(), "the Vue renders instead of throwing on the Échelle");
        const std::string error = vue.sidecarError().value_or("");
        check(error.find("calibration dropped") != std::string::npos &&
                  error.find("echelle treatment dropped") != std::string::npos &&
                  error.find("3840x2160") != std::string::npos && error.find("4x4") != std::string::npos,
              "sidecarError() names what was dropped and both sizes (got '" + error + "')");
        check(readFile(sidecar) == onDisk, "loading writes nothing: the sidecar is untouched");

        bool threw = false;
        try {
            vue.addTreatment(std::make_unique<EchelleTreatment>(Calibration{"x40", 2.5, 3840, 2160}));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw && vue.treatmentCount() == 1, "adding an Échelle measured at another frame size is refused");
        check(readFile(sidecar) == onDisk, "a refused treatment saves nothing");

        vue.addTreatment(std::make_unique<LevelsTreatment>(10, 240));
        check(readFile(sidecar.string() + ".mismatched") == onDisk,
              "the next save keeps the old sidecar aside as .mismatched instead of erasing the dropped Calibration");
        Vue reloaded = Vue::load(imagePath);
        check(!reloaded.sidecarError() && reloaded.treatmentCount() == 2 && !reloaded.calibration() &&
                  reloaded.captureInfo(),
              "the new sidecar reloads cleanly");
    }

    std::cout << "\n== Sidecars written before ADR 0014 still load ==\n";
    {
        const fs::path imagePath = dir / "old_vue.png";
        cv::imwrite(imagePath.string(), cv::Mat(4, 4, CV_8UC1, cv::Scalar(100)));
        const nlohmann::json levels = {{"black_point", 90}, {"white_point", 180}};
        std::ofstream(imagePath.string() + ".json")
            << nlohmann::json::array({{{"type", "levels"}, {"params", levels}}}).dump();
        Vue old = Vue::load(imagePath);
        check(old.treatmentCount() == 1, "bare-array sidecar: its treatment is loaded");
        check(!old.captureInfo().has_value(), "bare-array sidecar: no capture info, not an error");
        check(!old.calibration().has_value(), "bare-array sidecar: no Calibration, not an error");
    }

    fs::remove_all(dir);
    return summary();
}
