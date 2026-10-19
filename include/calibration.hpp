#pragma once

#include <nlohmann/json.hpp>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>

namespace msapp {

// Calibration (see CONTEXT.md): the pixels-per-µm ratio tied to one
// Grossissement. Measured against a mire reticle, reused for every future
// photo taken at that Grossissement — this struct is that reusable value,
// not a per-photo measurement. One objective measured on the mire is
// enough: the others are derived from it by the ratio of magnifications,
// as the legacy app did (ADR 0020).
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
    // The Grossissement whose mire Calibration this one was derived from
    // (ADR 0020); empty when it was itself measured on the mire.
    std::string derivedFrom = {};

    bool hasFrameSize() const { return frameWidth > 0 && frameHeight > 0; }
    bool isDerived() const { return !derivedFrom.empty(); }

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
        if (isDerived()) {
            return "Calibration " + grossissement + " (derived from " + derivedFrom + ") was measured at " +
                   measured + " but the image is " + actual + ". Recalibrate " + derivedFrom + " or " +
                   grossissement + " at " + actual + " or restore the camera mode it was measured in (ADR 0015).";
        }
        return "Calibration " + grossissement + " was measured at " + measured + " but the image is " + actual +
               ". Recalibrate " + grossissement + " at " + actual +
               " or restore the camera mode it was measured in (ADR 0015).";
    }

    // Throws std::runtime_error with frameMismatch()'s reason when !fitsFrame.
    // `context` says where it was about to be applied, e.g. "Vue::capture".
    void requireFitsFrame(int width, int height, const std::string& context) const {
        if (auto why = frameMismatch(width, height)) throw std::runtime_error(context + ": " + *why);
    }

    // "derived_from" only for a derived Calibration, so a measured one keeps
    // the layout of files written before ADR 0020.
    nlohmann::json toJson() const {
        nlohmann::json j = {{"grossissement", grossissement},
                            {"pixels_per_micron", pixelsPerMicron},
                            {"frame_width", frameWidth},
                            {"frame_height", frameHeight}};
        if (isDerived()) j["derived_from"] = derivedFrom;
        return j;
    }

    // Missing frame size keys load as unknown, so Échelle params written
    // before ADR 0015 still load; a missing "derived_from" means measured.
    static Calibration fromJson(const nlohmann::json& j) {
        Calibration c;
        c.grossissement = j.at("grossissement").get<std::string>();
        c.pixelsPerMicron = j.at("pixels_per_micron").get<double>();
        c.frameWidth = j.value("frame_width", 0);
        c.frameHeight = j.value("frame_height", 0);
        c.derivedFrom = j.value("derived_from", std::string());
        return c;
    }
};

// The magnification a Grossissement names: the first number in it, so
// "x10" -> 10, "x2.5" -> 2.5, "10x/0.25" -> 10. Empty when the name has no
// number, or it is 0.
inline std::optional<double> magnification(const std::string& grossissement) {
    const auto isDigit = [&](std::size_t i) {
        return i < grossissement.size() && std::isdigit(static_cast<unsigned char>(grossissement[i])) != 0;
    };
    std::size_t i = 0;
    while (i < grossissement.size() && !isDigit(i)) ++i;
    double value = 0.0;
    for (; isDigit(i); ++i) value = value * 10.0 + (grossissement[i] - '0');
    if (i < grossissement.size() && grossissement[i] == '.' && isDigit(i + 1)) {
        double scale = 0.1;
        for (++i; isDigit(i); ++i, scale /= 10.0) value += (grossissement[i] - '0') * scale;
    }
    if (!(value > 0.0)) return std::nullopt;
    return value;
}

// `measured` carried over to `grossissement` by the ratio of their
// magnifications, as the legacy app did (px/mm at 1x times G, ADR 0020):
// the camera and its adapter are the same for every objective, so px/µm
// scales with the objective alone. Keeps the frame size it was measured at
// (ADR 0015) and records where it came from. Empty when either name has no
// magnification.
inline std::optional<Calibration> deriveCalibration(const Calibration& measured, const std::string& grossissement) {
    const auto from = magnification(measured.grossissement);
    const auto to = magnification(grossissement);
    if (!from || !to) return std::nullopt;
    Calibration derived;
    derived.grossissement = grossissement;
    derived.pixelsPerMicron = measured.pixelsPerMicron / *from * *to;
    derived.frameWidth = measured.frameWidth;
    derived.frameHeight = measured.frameHeight;
    derived.derivedFrom = measured.grossissement;
    return derived;
}

} // namespace msapp
