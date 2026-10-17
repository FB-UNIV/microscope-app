#pragma once

#include "calibration.hpp"
#include "camera_factory.hpp"
#include "mire_calibration.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// Everything a workstation needs to capture the way it did yesterday: its
// Camera profile (with the microscope's White reference, ADR 0014) and the
// Calibrations measured on the mire (ADR 0015), from which every other
// Grossissement is derived (ADR 0020). Local to the PC and never
// synced: it describes the camera and microscope plugged into this
// workstation, unlike the Department/TP/Échantillon config of ADR 0007.
struct WorkstationConfig {
    CameraConfig camera = moticamA5Profile();
    // The Calibrations measured on the mire: at most one per Grossissement,
    // in the order they were first set. Use setCalibration():
    // saveWorkstationConfig() refuses a list it would not load back.
    std::vector<Calibration> calibrations;
    // The Grossissement the others are derived from (ADR 0020): the one most
    // recently measured on the mire. Empty, or naming no Calibration (a file
    // edited by hand), means the last one listed.
    std::string referenceGrossissement;
    // The period of this workstation's mire, in µm: the distance between two
    // neighbouring lines. 20 µm for the lab's mire; a stage micrometer of
    // 1 mm in 100 divisions is 10 µm. Set in the calibration dialog (#65).
    double mirePitchMicrons = kMirePitchMicrons;
    // Absolute folder the VueStore files Vues under; empty = defaultVueRoot().
    // Set it to put this workstation's Vues on another drive.
    std::filesystem::path vueRoot;

    // The mire Calibration of `grossissement`, whatever frame size it was
    // measured at; empty when that objective was never measured here.
    std::optional<Calibration> calibration(const std::string& grossissement) const;

    // The mire Calibration the others are derived from: the one
    // referenceGrossissement names, else the last listed; empty when no
    // objective was ever measured here.
    std::optional<Calibration> reference() const;

    // The Calibration of `grossissement`, whatever frame size it holds for:
    // its own mire Calibration when it has one, otherwise one derived from
    // reference() by the ratio of magnifications (ADR 0020). Empty when no
    // objective was ever measured here, or `grossissement` names no
    // magnification.
    std::optional<Calibration> effectiveCalibration(const std::string& grossissement) const;

    // The Calibration to capture with at `grossissement` (effectiveCalibration),
    // checked against the width x height frame the camera actually delivers
    // (ProfileReport::frameSize, or a grabbed frame). Empty when there is
    // none — the capture is simply uncalibrated. Throws std::runtime_error
    // naming both sizes when it was measured at another frame size: the
    // operator recalibrates or restores the camera mode, nothing is rescaled
    // (ADR 0015).
    std::optional<Calibration> calibrationForFrame(const std::string& grossissement, int width, int height) const;

    // Adds the Calibration of calibration.grossissement, or replaces it, and
    // makes it the reference the others are derived from: recalibrating an
    // objective — at the same or another frame size — changes only future
    // Vues, which is fine because every Vue keeps its own copy in its
    // sidecar. Throws std::invalid_argument for an empty Grossissement, a
    // ratio that isn't a positive number, no frame size (the calibration
    // wizard must record the size it measured at, ADR 0015), or a derived
    // Calibration: only mire measurements are kept, derived ones are
    // recomputed.
    void setCalibration(const Calibration& calibration);

    // Forgets the Calibration of `grossissement`; false if there was none.
    // Forgetting the reference makes the last one listed the reference.
    bool removeCalibration(const std::string& grossissement);

    nlohmann::json toJson() const;
    // Missing "camera", "calibrations", "reference_grossissement" or
    // "mire_pitch_microns" keys (a pitch that isn't a positive number too)
    // take their defaults, so older files load. Throws on anything
    // malformed, including a Calibration setCalibration() would refuse or a
    // Grossissement listed twice.
    static WorkstationConfig fromJson(const nlohmann::json& j);
};

// Where this workstation keeps its config: <per-user local app data>/
// microscope-app/workstation.json — %LOCALAPPDATA% on Windows, not the
// roaming %APPDATA%: a White reference and Calibrations belong to the
// microscope on this PC and must not follow a roaming profile to another
// one. $XDG_CONFIG_HOME (default ~/.config) elsewhere. Throws
// std::runtime_error when the environment names no such folder.
std::filesystem::path defaultWorkstationConfigPath();

// What loadWorkstationConfig() read.
struct LoadedWorkstationConfig {
    WorkstationConfig config;
    // Why the file couldn't be read, naming it; empty when it loaded or
    // didn't exist yet.
    std::string error;
};

// Reads the config at `path`. A missing file is a workstation's first launch:
// defaults (the Moticam A5 profile, identity White reference, no
// Calibrations) and no error. A file that can't be read doesn't stop the app
// either: defaults, with `error` saying why. The file is left as is, and the
// next saveWorkstationConfig() keeps it aside rather than overwriting it.
LoadedWorkstationConfig loadWorkstationConfig(const std::filesystem::path& path);

// Writes `config` to `path`, creating its folder, atomically: a crash or a
// full disk mid-write leaves the previous file, never a truncated one. An
// existing file that loadWorkstationConfig() can't read is first moved aside
// to <path>.unreadable (.2, .3, ... if taken) instead of being overwritten —
// it may still hold the only record of a White reference or Calibrations
// (ADR 0004). Throws std::invalid_argument, writing nothing, for a config
// fromJson() would refuse, and std::runtime_error when the write fails.
void saveWorkstationConfig(const WorkstationConfig& config, const std::filesystem::path& path);

} // namespace msapp
