// User journey:
//   As a student at a lab workstation, every photo I capture comes out with
//   the same exposure and colour as my neighbour's — the camera profile is
//   re-applied whenever the camera opens (the Moticam A5 keeps the last
//   program's settings otherwise), the microscope's White reference is baked
//   in, and the photo's sidecar records exactly how it was taken (ADR 0014).

#include "capture_station.hpp"
#include "levels_treatment.hpp"
#include "vue.hpp"

#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace msapp;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  [PASS] " << description << "\n";
    } else {
        std::cout << "  [FAIL] " << description << "\n";
        ++g_failures;
    }
}

bool nearlyEqual(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

// Records which controls the station applied; frames are a fixed tinted field.
class FakeCamera : public ICamera {
public:
    FakeCamera(cv::Mat frame, bool acceptsControls) : frame_(std::move(frame)), accepts_(acceptsControls) {}

    std::string backend() const override { return "fake"; }
    void open() override { open_ = true; }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    cv::Mat grab() override {
        if (!open_) throw std::runtime_error("FakeCamera: not open");
        return frame_.clone();
    }
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
        }

        bool threw = false;
        try {
            station.capture(imagePath);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "capturing onto an existing path still refuses (Vues are immutable-by-path)");
    }

    std::cout << "\n== Sidecars written before ADR 0014 still load ==\n";
    {
        const fs::path imagePath = dir / "old_vue.png";
        cv::imwrite(imagePath.string(), cv::Mat(4, 4, CV_8UC1, cv::Scalar(100)));
        LevelsTreatment levels(90, 180);  // also links the "levels" creator into this test
        std::ofstream(imagePath.string() + ".json")
            << nlohmann::json::array({{{"type", "levels"}, {"params", levels.paramsToJson()}}}).dump();
        Vue old = Vue::load(imagePath);
        check(old.treatmentCount() == 1, "bare-array sidecar: its treatment is loaded");
        check(!old.captureInfo().has_value(), "bare-array sidecar: no capture info, not an error");
    }

    fs::remove_all(dir);
    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
