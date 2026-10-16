#include "workstation_config.hpp"
#include "safe_write.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace msapp {

using detail::unusedAsidePath;
using detail::writeFileAtomically;

namespace {

// Why `c` can't be kept in a workstation config; empty when it can.
std::optional<std::string> invalidCalibration(const Calibration& c) {
    if (c.grossissement.empty()) {
        return std::string("a Calibration has no Grossissement");
    }
    // Recomputed from the reference whenever it is needed (ADR 0020).
    if (c.isDerived()) {
        return "Calibration " + c.grossissement + " is derived from " + c.derivedFrom +
               "; only Calibrations measured on the mire are kept";
    }
    if (!std::isfinite(c.pixelsPerMicron) || c.pixelsPerMicron <= 0.0) {
        return "Calibration " + c.grossissement + " has pixels_per_micron " + std::to_string(c.pixelsPerMicron) +
               ", expected a positive number";
    }
    // Without a frame size it would hold for any camera mode (ADR 0015).
    if (!c.hasFrameSize()) {
        return "Calibration " + c.grossissement + " records no frame size — recalibrate " + c.grossissement +
               " so the size it is measured at is kept (ADR 0015)";
    }
    return std::nullopt;
}

// Throws std::invalid_argument starting with `context` unless every
// Calibration is valid and no Grossissement appears twice.
void requireValidCalibrations(const std::vector<Calibration>& calibrations, const std::string& context) {
    for (std::size_t i = 0; i < calibrations.size(); ++i) {
        if (auto why = invalidCalibration(calibrations[i])) {
            throw std::invalid_argument(context + ": " + *why);
        }
        for (std::size_t k = 0; k < i; ++k) {
            if (calibrations[k].grossissement == calibrations[i].grossissement) {
                throw std::invalid_argument(context + ": Grossissement " + calibrations[i].grossissement +
                                            " has two Calibrations, expected at most one");
            }
        }
    }
}

// Throws whatever stops `path` loading as a WorkstationConfig.
WorkstationConfig readConfigFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open the file");
    }
    nlohmann::json j;
    in >> j;
    return WorkstationConfig::fromJson(j);
}

// An absolute folder named by an environment variable; empty when unset,
// empty or relative (the XDG spec says to ignore a relative one).
#ifdef _WIN32
std::filesystem::path folderFromEnvironment(const wchar_t* name) {
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::filesystem::path folder(value);
    std::free(value);
    return folder.is_absolute() ? folder : std::filesystem::path();
}
#else
std::filesystem::path folderFromEnvironment(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return {};
    }
    std::filesystem::path folder(value);
    return folder.is_absolute() ? folder : std::filesystem::path();
}
#endif

} // namespace

std::optional<Calibration> WorkstationConfig::calibration(const std::string& grossissement) const {
    auto it = std::find_if(calibrations.begin(), calibrations.end(),
                           [&](const Calibration& c) { return c.grossissement == grossissement; });
    if (it == calibrations.end()) return std::nullopt;
    return *it;
}

std::optional<Calibration> WorkstationConfig::reference() const {
    if (auto named = calibration(referenceGrossissement)) return named;
    if (calibrations.empty()) return std::nullopt;
    return calibrations.back();
}

std::optional<Calibration> WorkstationConfig::effectiveCalibration(const std::string& grossissement) const {
    if (auto own = calibration(grossissement)) return own;
    const auto measured = reference();
    if (!measured) return std::nullopt;
    return deriveCalibration(*measured, grossissement);
}

std::optional<Calibration> WorkstationConfig::calibrationForFrame(const std::string& grossissement, int width,
                                                                  int height) const {
    auto found = effectiveCalibration(grossissement);
    if (found) {
        found->requireFitsFrame(width, height, "WorkstationConfig::calibrationForFrame");
    }
    return found;
}

void WorkstationConfig::setCalibration(const Calibration& calibration) {
    if (auto why = invalidCalibration(calibration)) {
        throw std::invalid_argument("WorkstationConfig::setCalibration: " + *why);
    }
    auto it = std::find_if(calibrations.begin(), calibrations.end(),
                           [&](const Calibration& c) { return c.grossissement == calibration.grossissement; });
    if (it == calibrations.end()) {
        calibrations.push_back(calibration);
    } else {
        *it = calibration;
    }
    referenceGrossissement = calibration.grossissement;
}

bool WorkstationConfig::removeCalibration(const std::string& grossissement) {
    auto it = std::find_if(calibrations.begin(), calibrations.end(),
                           [&](const Calibration& c) { return c.grossissement == grossissement; });
    if (it == calibrations.end()) return false;
    calibrations.erase(it);
    if (referenceGrossissement == grossissement) referenceGrossissement.clear();
    return true;
}

nlohmann::json WorkstationConfig::toJson() const {
    nlohmann::json calibrationsJson = nlohmann::json::array();
    for (const auto& c : calibrations) calibrationsJson.push_back(c.toJson());
    return {{"camera", camera.toJson()},
            {"calibrations", calibrationsJson},
            {"reference_grossissement", referenceGrossissement},
            {"vue_root", vueRoot.u8string()}};
}

WorkstationConfig WorkstationConfig::fromJson(const nlohmann::json& j) {
    // Checked up front: a stray array or number would otherwise read as
    // "every key missing" and load silently as defaults.
    if (!j.is_object()) {
        throw std::invalid_argument("WorkstationConfig::fromJson: expected a JSON object");
    }
    WorkstationConfig config;
    if (j.contains("camera")) {
        config.camera = CameraConfig::fromJson(j.at("camera"));
    }
    if (j.contains("calibrations")) {
        const nlohmann::json& entries = j.at("calibrations");
        if (!entries.is_array()) {
            throw std::invalid_argument("WorkstationConfig::fromJson: \"calibrations\" must be an array");
        }
        for (const auto& entry : entries) config.calibrations.push_back(Calibration::fromJson(entry));
    }
    if (j.contains("vue_root")) {
        config.vueRoot = std::filesystem::u8path(j.at("vue_root").get<std::string>());
    }
    // A name matching no Calibration falls back to the last one listed
    // (reference()) rather than refusing the whole file and its White
    // reference with it.
    config.referenceGrossissement = j.value("reference_grossissement", std::string());
    requireValidCalibrations(config.calibrations, "WorkstationConfig::fromJson");
    return config;
}

std::filesystem::path defaultWorkstationConfigPath() {
#ifdef _WIN32
    const std::filesystem::path base = folderFromEnvironment(L"LOCALAPPDATA");
    if (base.empty()) {
        throw std::runtime_error("defaultWorkstationConfigPath: %LOCALAPPDATA% is not set");
    }
#else
    std::filesystem::path base = folderFromEnvironment("XDG_CONFIG_HOME");
    if (base.empty()) {
        const std::filesystem::path home = folderFromEnvironment("HOME");
        if (home.empty()) {
            throw std::runtime_error("defaultWorkstationConfigPath: neither $XDG_CONFIG_HOME nor $HOME is set");
        }
        base = home / ".config";
    }
#endif
    return base / "microscope-app" / "workstation.json";
}

LoadedWorkstationConfig loadWorkstationConfig(const std::filesystem::path& path) {
    LoadedWorkstationConfig loaded;
    try {
        if (!std::filesystem::exists(path)) {
            return loaded;  // first launch on this workstation: defaults
        }
        loaded.config = readConfigFile(path);
    } catch (const std::exception& e) {
        // Never blocks the app; saveWorkstationConfig() keeps the file aside.
        loaded.error = path.string() + ": " + e.what();
    }
    return loaded;
}

void saveWorkstationConfig(const WorkstationConfig& config, const std::filesystem::path& path) {
    // Never write a file the next launch would refuse to load.
    requireValidCalibrations(config.calibrations, "saveWorkstationConfig");

    // Never overwrite a file load could not read: it may still be the only
    // record of a White reference or Calibrations (ADR 0004).
    if (std::filesystem::exists(path)) {
        try {
            readConfigFile(path);
        } catch (const std::exception&) {
            std::filesystem::rename(path, unusedAsidePath(path, ".unreadable"));
        }
    }

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    writeFileAtomically(path, config.toJson().dump(2), "saveWorkstationConfig");
}

} // namespace msapp
