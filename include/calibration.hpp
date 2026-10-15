#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace msapp {

// Calibration (see CONTEXT.md): the pixels-per-µm ratio tied to one
// Grossissement. Set once per objective against a mire reticle, reused for
// every future photo taken at that Grossissement — this struct is that
// reusable value, not a per-photo measurement.
//
// The ratio only holds for the frame size it was measured at (ADR 0015):
// a camera that falls back to another mode would make every Measurement,
// Échelle and Vickers reading wrong by the scale factor. Applying it to an
// image of another size is refused, never rescaled.
struct Calibration {
    std::string grossissement;   // e.g. "x40" — which objective this applies to
    double pixelsPerMicron = 0.0;
    int frameWidth = 0;          // frame size it was measured at; 0 = unknown
    int frameHeight = 0;         //   (Calibrations stored before ADR 0015)

    bool hasFrameSize() const { return frameWidth > 0 && frameHeight > 0; }

    // True when this Calibration holds for a width x height image: same frame
    // size, or no frame size recorded (accepted unverified, ADR 0015).
    bool fitsFrame(int width, int height) const {
        return !hasFrameSize() || (width == frameWidth && height == frameHeight);
    }

    // Why this Calibration doesn't hold for a width x height image, naming
    // both sizes; empty when it fits.
    std::optional<std::string> frameMismatch(int width, int height) const {
        if (fitsFrame(width, height)) return std::nullopt;
        const std::string measured = std::to_string(frameWidth) + "x" + std::to_string(frameHeight);
        const std::string actual = std::to_string(width) + "x" + std::to_string(height);
        return "Calibration " + grossissement + " was measured at " + measured + " but the image is " + actual +
               ". Recalibrate " + grossissement + " at " + actual +
               " or restore the camera mode it was measured in (ADR 0015).";
    }

    // Throws std::runtime_error with frameMismatch()'s reason when !fitsFrame.
    // `context` says where it was about to be applied, e.g. "Vue::capture".
    void requireFitsFrame(int width, int height, const std::string& context) const {
        if (auto why = frameMismatch(width, height)) throw std::runtime_error(context + ": " + *why);
    }

    nlohmann::json toJson() const {
        return {{"grossissement", grossissement},
                {"pixels_per_micron", pixelsPerMicron},
                {"frame_width", frameWidth},
                {"frame_height", frameHeight}};
    }

    // Missing frame size keys load as unknown, so Échelle params written
    // before ADR 0015 still load.
    static Calibration fromJson(const nlohmann::json& j) {
        Calibration c;
        c.grossissement = j.at("grossissement").get<std::string>();
        c.pixelsPerMicron = j.at("pixels_per_micron").get<double>();
        c.frameWidth = j.value("frame_width", 0);
        c.frameHeight = j.value("frame_height", 0);
        return c;
    }
};

} // namespace msapp
