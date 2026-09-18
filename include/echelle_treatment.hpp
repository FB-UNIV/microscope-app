#pragma once

#include "treatment.hpp"
#include "calibration.hpp"

namespace msapp {

// Échelle (see CONTEXT.md): the visual scale-bar overlay rendered onto a
// photo, built from its Calibration. A Treatment — it mutates the saved
// photo (as opposed to Measurement, the separate ephemeral point-to-point
// tool, which does not).
//
// Draws a horizontal bar sized to a "nice" round µm length (1/2/5 x 10^n —
// standard microscopy scale-bar convention) close to image_width / 6,
// with a text label, in the bottom-left corner.
class EchelleTreatment : public Treatment {
public:
    explicit EchelleTreatment(Calibration calibration);

    std::string type() const override { return "echelle"; }
    cv::Mat apply(const cv::Mat& input) const override;
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
