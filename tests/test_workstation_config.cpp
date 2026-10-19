// User journey:
//   As the teacher who set up a lab workstation, I calibrate each objective
//   and the microscope's White reference once; after a restart, the camera
//   profile, the White reference and every Calibration are still there. If
//   the camera came back in another mode, the app refuses to measure with a
//   Calibration taken at the old size instead of silently getting every
//   length wrong (ADR 0015), and a damaged config file is kept aside rather
//   than overwritten (ADR 0004).

#include "workstation_config.hpp"
#include "test_support.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

const Calibration kX10At4k{"x10", 0.62, 3840, 2160};
const Calibration kX40At4k{"x40", 2.5, 3840, 2160};

// A workstation as the setup UI leaves it: measured exposure, a calibrated
// White reference, two objectives calibrated at the profile's 4K mode.
WorkstationConfig configuredWorkstation() {
    WorkstationConfig config;
    config.camera.exposureMs = 33.3;
    config.camera.whiteReference = WhiteBalance{1.745, 1.0, 2.576};
    config.setCalibration(kX10At4k);
    config.setCalibration(kX40At4k);
    return config;
}

bool sameCalibration(const Calibration& a, const Calibration& b) {
    return a.grossissement == b.grossissement && a.pixelsPerMicron == b.pixelsPerMicron &&
           a.frameWidth == b.frameWidth && a.frameHeight == b.frameHeight;
}

// Environment access for the default-path checks; an empty value unsets.
#ifdef _WIN32
std::string environment(const char* name) {
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string result(value);
    std::free(value);
    return result;
}
void setEnvironment(const char* name, const std::string& value) {
    _putenv_s(name, value.c_str());
}
#else
std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
void setEnvironment(const char* name, const std::string& value) {
    if (value.empty()) {
        unsetenv(name);
    } else {
        setenv(name, value.c_str(), 1);
    }
}
#endif

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_workstation_config_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::cout << "== Defaults: a workstation's first launch ==\n";
    {
        const WorkstationConfig config;
        const CameraConfig a5 = moticamA5Profile();
        check(config.camera.backend == a5.backend && config.camera.captureApi == a5.captureApi &&
                  config.camera.width == a5.width && config.camera.height == a5.height &&
                  config.camera.pixelFormat == a5.pixelFormat &&
                  config.camera.whiteBalanceKelvin == a5.whiteBalanceKelvin,
              "camera profile defaults to the Moticam A5 profile");
        check(config.camera.whiteReference.isIdentity(), "White reference identity until calibrated");
        check(config.calibrations.empty(), "no Calibration until an objective is calibrated");
    }

    std::cout << "\n== Calibrations: one per Grossissement, tied to a frame size (ADR 0015) ==\n";
    {
        WorkstationConfig config = configuredWorkstation();
        check(config.calibrations.size() == 2, "two objectives calibrated");
        auto x40 = config.calibration("x40");
        check(x40 && sameCalibration(*x40, kX40At4k), "looked up by Grossissement");
        check(!config.calibration("x100"), "an objective never calibrated has no Calibration");

        config.setCalibration(Calibration{"x40", 2.51, 3840, 2160});
        check(config.calibrations.size() == 2 && config.calibration("x40")->pixelsPerMicron == 2.51,
              "recalibrating an objective replaces its Calibration");
        check(config.calibrations[0].grossissement == "x10" && config.calibrations[1].grossissement == "x40",
              "replacing keeps the order objectives were first calibrated in");

        check(config.removeCalibration("x10") && !config.calibration("x10") && config.calibrations.size() == 1,
              "removeCalibration forgets one objective");
        check(!config.removeCalibration("x10"), "removing an uncalibrated objective reports false");
    }
    {
        WorkstationConfig config;
        auto refused = [&](const Calibration& c) {
            return !throwsWith<std::invalid_argument>([&] { config.setCalibration(c); }).empty();
        };
        check(refused(Calibration{"x40", 2.5, 0, 0}),
              "a Calibration without the frame size it was measured at is refused");
        check(refused(Calibration{"", 2.5, 3840, 2160}), "a Calibration without a Grossissement is refused");
        check(refused(Calibration{"x40", 0.0, 3840, 2160}) && refused(Calibration{"x40", -2.5, 3840, 2160}) &&
                  refused(Calibration{"x40", std::nan(""), 3840, 2160}),
              "a ratio that isn't a positive number is refused");
        check(config.calibrations.empty(), "a refused Calibration is not stored");
    }

    std::cout << "\n== calibrationForFrame: checked against the camera's actual frame size ==\n";
    {
        const WorkstationConfig config = configuredWorkstation();
        auto atProfileMode = config.calibrationForFrame("x40", 3840, 2160);
        check(atProfileMode && sameCalibration(*atProfileMode, kX40At4k),
              "the Calibration is returned when the camera delivers the size it was measured at");
        check(!WorkstationConfig{}.calibrationForFrame("x100", 1024, 768),
              "with no objective measured, a capture is uncalibrated");

        // The A5 falls back to 1024x768 when a driver can't grant the 4K mode.
        const std::string why = throwsWith<std::runtime_error>([&] { config.calibrationForFrame("x40", 1024, 768); });
        check(!why.empty(), "a Calibration measured at another frame size is refused, not rescaled");
        check(why.find("3840x2160") != std::string::npos && why.find("1024x768") != std::string::npos &&
                  why.find("x40") != std::string::npos,
              "the refusal names the Grossissement and both frame sizes");
    }

    std::cout << "\n== Derived Calibrations: one objective on the mire is enough (ADR 0020) ==\n";
    {
        check(magnification("x10") == 10.0 && magnification("X40") == 40.0 && magnification("x2.5") == 2.5 &&
                  magnification("10x/0.25") == 10.0,
              "a Grossissement's magnification is the first number in its name");
        check(!magnification("Macro") && !magnification("x0") && !magnification(""),
              "a name with no number, or 0, has none");

        WorkstationConfig config;
        config.setCalibration(Calibration{"x5", 0.31, 3840, 2160});
        const auto x10 = config.effectiveCalibration("x10");
        check(x10 && x10->isDerived() && x10->derivedFrom == "x5" && nearlyEqual(x10->pixelsPerMicron, 0.62) &&
                  x10->frameWidth == 3840 && x10->frameHeight == 2160,
              "with only x5 measured, x10 gets twice its px/µm, at the frame size x5 was measured at");
        check(nearlyEqual(config.effectiveCalibration("x50")->pixelsPerMicron, 3.1) &&
                  nearlyEqual(config.effectiveCalibration("x2.5")->pixelsPerMicron, 0.155),
              "every objective scales by its magnification, up or down");
        check(!config.calibration("x10") && config.calibrations.size() == 1,
              "a derived Calibration is computed, never stored");
        check(!config.effectiveCalibration("x5")->isDerived(), "the measured objective uses its own Calibration");
        check(!config.effectiveCalibration("Macro"), "a Grossissement with no magnification stays uncalibrated");

        const auto atMode = config.calibrationForFrame("x10", 3840, 2160);
        check(atMode && atMode->isDerived() && nearlyEqual(atMode->pixelsPerMicron, 0.62),
              "calibrationForFrame hands out the derived Calibration for capture");
        const std::string why = throwsWith<std::runtime_error>([&] { config.calibrationForFrame("x10", 1024, 768); });
        check(why.find("derived from x5") != std::string::npos && why.find("Recalibrate x5") != std::string::npos &&
                  why.find("1024x768") != std::string::npos,
              "a derived Calibration is checked against the frame size too, and says which objective to recalibrate");

        config.setCalibration(Calibration{"x10", 0.6, 3840, 2160});
        check(!config.effectiveCalibration("x10")->isDerived() &&
                  nearlyEqual(config.effectiveCalibration("x10")->pixelsPerMicron, 0.6),
              "an objective measured on the mire uses its own Calibration over a derived one");
        check(config.reference()->grossissement == "x10" && config.effectiveCalibration("x50")->derivedFrom == "x10" &&
                  nearlyEqual(config.effectiveCalibration("x50")->pixelsPerMicron, 3.0),
              "the objective measured last is the reference the others are derived from");
        config.setCalibration(Calibration{"x5", 0.31, 3840, 2160});
        check(config.reference()->grossissement == "x5", "measuring x5 again makes it the reference again");
        config.removeCalibration("x5");
        check(config.reference()->grossissement == "x10", "forgetting the reference falls back to the last listed");

        check(!throwsWith<std::invalid_argument>([&] { config.setCalibration(*config.effectiveCalibration("x40")); })
                   .empty(),
              "a derived Calibration can't be stored as if measured");

        const Calibration derived = *config.effectiveCalibration("x40");
        const Calibration back = Calibration::fromJson(derived.toJson());
        check(back.derivedFrom == "x10" && nearlyEqual(back.pixelsPerMicron, derived.pixelsPerMicron),
              "a derived Calibration keeps where it came from through JSON, so a Vue's sidecar can say so");
        check(!kX40At4k.toJson().contains("derived_from") && !Calibration::fromJson(kX40At4k.toJson()).isDerived(),
              "a measured one is written as before, with no \"derived_from\"");
    }

    std::cout << "\n== JSON layout ==\n";
    {
        const nlohmann::json j = configuredWorkstation().toJson();
        const nlohmann::json& camera = j.at("camera");
        check(camera.at("capture_api") == "dshow" && camera.at("width") == 3840 && camera.at("height") == 2160 &&
                  camera.at("pixel_format") == "MJPG" && camera.at("exposure_ms") == 33.3 &&
                  camera.at("white_balance_kelvin") == 5000.0 && camera.at("white_reference").at("blue") == 2.576,
              "\"camera\" holds the profile and the White reference");
        check(j.at("calibrations").size() == 2 && j.at("calibrations")[1] == kX40At4k.toJson(),
              "\"calibrations\" lists {grossissement, pixels_per_micron, frame_width, frame_height}");

        const WorkstationConfig empty = WorkstationConfig::fromJson(nlohmann::json::object());
        check(empty.camera.backend == "opencv" && empty.camera.width == 3840 && empty.calibrations.empty(),
              "missing keys take their defaults");
        check(j.at("reference_grossissement") == "x40", "\"reference_grossissement\" names the reference");
        nlohmann::json older = j;
        older.erase("reference_grossissement");
        nlohmann::json edited = j;
        edited["reference_grossissement"] = "x63";
        check(WorkstationConfig::fromJson(older).reference()->grossissement == "x40" &&
                  WorkstationConfig::fromJson(edited).reference()->grossissement == "x40",
              "a file without it, or naming no Calibration, derives from the last one listed");

        check(empty.mirePitchMicrons == 20.0 && j.at("mire_pitch_microns") == 20.0,
              "the mire's period defaults to the lab's 20 µm");
        WorkstationConfig micrometer;
        micrometer.mirePitchMicrons = 10.0;
        check(WorkstationConfig::fromJson(micrometer.toJson()).mirePitchMicrons == 10.0,
              "another mire's period round-trips (#65)");
        nlohmann::json badPitch = j;
        badPitch["mire_pitch_microns"] = -5.0;
        nlohmann::json textPitch = j;
        textPitch["mire_pitch_microns"] = "ten";
        check(WorkstationConfig::fromJson(badPitch).mirePitchMicrons == 20.0 &&
                  WorkstationConfig::fromJson(textPitch).mirePitchMicrons == 20.0,
              "a period that isn't a positive number falls back to 20 µm instead of losing the file");

        nlohmann::json twice = j;
        twice["calibrations"].push_back(Calibration{"x40", 2.6, 1024, 768}.toJson());
        check(!throwsWith<std::invalid_argument>([&] { WorkstationConfig::fromJson(twice); }).empty(),
              "a Grossissement listed twice is refused, not resolved by guessing");
        nlohmann::json unsized = j;
        unsized["calibrations"][0].erase("frame_width");
        unsized["calibrations"][0].erase("frame_height");
        check(!throwsWith<std::invalid_argument>([&] { WorkstationConfig::fromJson(unsized); }).empty(),
              "a Calibration without a frame size is refused on load too");
        check(!throwsWith<std::invalid_argument>([&] { WorkstationConfig::fromJson(nlohmann::json::array()); })
                   .empty(),
              "a JSON array is refused instead of loading as defaults");
    }

    std::cout << "\n== Disk: settings survive a restart ==\n";
    {
        const fs::path path = dir / "app data" / "microscope-app" / "workstation.json";
        const WorkstationConfig saved = configuredWorkstation();
        saveWorkstationConfig(saved, path);
        check(fs::exists(path), "save creates the per-user folder and the file");
        check(!fs::exists(path.string() + ".tmp"), "no temp file left behind");

        // "Restart": nothing shared with the saved object but the file.
        const LoadedWorkstationConfig loaded = loadWorkstationConfig(path);
        const WorkstationConfig& back = loaded.config;
        check(loaded.error.empty(), "the saved file loads without error");
        check(back.camera.backend == saved.camera.backend && back.camera.deviceIndex == saved.camera.deviceIndex &&
                  back.camera.captureApi == "dshow",
              "capture API round-trips through disk");
        check(back.camera.width == 3840 && back.camera.height == 2160 && back.camera.pixelFormat == "MJPG",
              "frame size and pixel format round-trip through disk");
        check(back.camera.exposureMs && *back.camera.exposureMs == 33.3 && back.camera.whiteBalanceKelvin &&
                  *back.camera.whiteBalanceKelvin == 5000.0,
              "exposure and colour temperature round-trip through disk");
        check(back.camera.whiteReference.red == 1.745 && back.camera.whiteReference.green == 1.0 &&
                  back.camera.whiteReference.blue == 2.576,
              "White reference round-trips through disk");
        check(back.calibrations.size() == 2 && sameCalibration(back.calibrations[0], kX10At4k) &&
                  sameCalibration(back.calibrations[1], kX40At4k),
              "Calibrations round-trip through disk, frame size included");
        check(back.referenceGrossissement == "x40" && back.effectiveCalibration("x100")->derivedFrom == "x40",
              "so does the reference the other objectives are derived from");
        check(!throwsWith<std::runtime_error>([&] { back.calibrationForFrame("x40", 1024, 768); }).empty(),
              "a reloaded Calibration is still checked against the frame size");

        WorkstationConfig changed = back;
        changed.setCalibration(Calibration{"x100", 6.2, 3840, 2160});
        saveWorkstationConfig(changed, path);
        check(loadWorkstationConfig(path).config.calibrations.size() == 3, "saving again replaces the file");
        check(!fs::exists(path.string() + ".unreadable"), "a readable file is replaced, not set aside");
    }
    {
        const LoadedWorkstationConfig first = loadWorkstationConfig(dir / "never-saved" / "workstation.json");
        check(first.error.empty() && first.config.calibrations.empty() && first.config.camera.backend == "opencv",
              "no file yet: defaults, and that is not an error");
    }

    std::cout << "\n== Disk: a damaged file costs nothing (ADR 0004) ==\n";
    {
        const fs::path path = dir / "damaged" / "workstation.json";
        fs::create_directories(path.parent_path());
        const std::string damaged = "{\"camera\": {\"backend\": \"opencv\", \"white_reference\": {\"red\": 1.7";
        writeFile(path, damaged);

        const LoadedWorkstationConfig loaded = loadWorkstationConfig(path);
        check(!loaded.error.empty() && loaded.error.find(path.string()) != std::string::npos,
              "an unreadable file loads as defaults with an error naming it");
        check(loaded.config.calibrations.empty() && loaded.config.camera.whiteReference.isIdentity(),
              "nothing half-read from the damaged file is used");
        check(readFile(path) == damaged, "loading leaves the damaged file untouched");

        saveWorkstationConfig(configuredWorkstation(), path);
        const fs::path aside = path.string() + ".unreadable";
        check(fs::exists(aside) && readFile(aside) == damaged,
              "saving keeps the damaged file aside as .unreadable instead of overwriting it");
        check(loadWorkstationConfig(path).error.empty(), "the new file loads");

        writeFile(path, "[]");
        saveWorkstationConfig(configuredWorkstation(), path);
        check(fs::exists(path.string() + ".unreadable.2") && readFile(aside) == damaged,
              "a second damaged file is kept as .unreadable.2, the first one stays");
    }
    {
        const fs::path path = dir / "invalid" / "workstation.json";
        saveWorkstationConfig(configuredWorkstation(), path);
        const std::string before = readFile(path);

        WorkstationConfig invalid = configuredWorkstation();
        invalid.calibrations.push_back(Calibration{"x40", 9.9, 3840, 2160});  // bypasses setCalibration
        check(!throwsWith<std::invalid_argument>([&] { saveWorkstationConfig(invalid, path); }).empty(),
              "a config the next launch would refuse is not saved");
        check(readFile(path) == before, "the previous file is kept as it was");
    }

    std::cout << "\n== defaultWorkstationConfigPath: the per-user local app data folder ==\n";
    {
#ifdef _WIN32
        const char* variable = "LOCALAPPDATA";
#else
        const char* variable = "XDG_CONFIG_HOME";
#endif
        const std::string original = environment(variable);
        const fs::path appData = dir / "local app data";
        setEnvironment(variable, appData.string());
        check(defaultWorkstationConfigPath() == appData / "microscope-app" / "workstation.json",
              std::string("lives in ") + variable + "/microscope-app/");
#ifndef _WIN32
        const std::string originalHome = environment("HOME");
        setEnvironment("XDG_CONFIG_HOME", "relative/config");
        setEnvironment("HOME", appData.string());
        check(defaultWorkstationConfigPath() == appData / ".config" / "microscope-app" / "workstation.json",
              "a relative $XDG_CONFIG_HOME is ignored for ~/.config (XDG spec)");
        setEnvironment("HOME", originalHome);
#endif
        setEnvironment(variable, original);
    }

    fs::remove_all(dir);
    return summary();
}
