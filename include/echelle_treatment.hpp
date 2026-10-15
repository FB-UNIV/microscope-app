#pragma once

#include "treatment.hpp"
#include "calibration.hpp"

namespace msapp {

// Échelle (see CONTEXT.md): the visual scale-bar overlay rendered onto a
// photo, built from its Calibration. A Treatment: drawn onto the rendered
// photo, never into the stored original (ADR 0003) — unlike Measurement,
// the ephemeral point-to-point tool, which draws nothing at all.
//
// Draws a horizontal bar sized to a "nice" round µm length (1/2/5 x 10^n —
// standard microscopy scale-bar convention) close to image_width / 6,
// with a text label, in the bottom-left corner.
class EchelleTreatment : public Treatment {
public:
    // Throws std::invalid_argument unless calibration.pixelsPerMicron is
    // finite and > 0 (a sidecar or legacy tag can carry 0).
    explicit EchelleTreatment(Calibration calibration);

    std::string type() const override { return "echelle"; }
    // Throws std::runtime_error when the Calibration was measured at another
    // frame size than `input` — the bar would show a false length (ADR 0015).
    cv::Mat apply(const cv::Mat& input) const override;
    std::optional<std::string> frameMismatch(int width, int height) const override {
        return calibration_.frameMismatch(width, height);
    }
    nlohmann::json paramsToJson() const override;

    static std::unique_ptr<Treatment> fromJson(const nlohmann::json& params);

    const Calibration& calibration() const { return calibration_; }

    // Exposed for testing: picks the nearest "nice" bar length in µm
    // (1/2/5 x 10^n) to targetPixels wide, given the pixels-per-µm ratio.
    static double niceBarLengthMicrons(double targetPixels, double pixelsPerMicron);

private:
    Calibration calibration_;
};

} // namespace msapp
