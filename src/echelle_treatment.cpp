#include "echelle_treatment.hpp"
#include "format_microns.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace msapp {

EchelleTreatment::EchelleTreatment(Calibration calibration) : calibration_(std::move(calibration)) {
    // Rejects NaN and infinity too: the bar length would be non-finite, and
    // converting that to int pixels in apply() is undefined behaviour.
    if (!std::isfinite(calibration_.pixelsPerMicron) || calibration_.pixelsPerMicron <= 0.0) {
        throw std::invalid_argument("EchelleTreatment: Calibration.pixelsPerMicron must be > 0, got " +
                                    std::to_string(calibration_.pixelsPerMicron));
    }
}

double EchelleTreatment::niceBarLengthMicrons(double targetPixels, double pixelsPerMicron) {
    const double targetMicrons = targetPixels / pixelsPerMicron;
    const double exponent = std::floor(std::log10(targetMicrons));
    const double base = std::pow(10.0, exponent);
    const double fraction = targetMicrons / base;

    double nice;
    if (fraction < 1.5) nice = 1.0;
    else if (fraction < 3.5) nice = 2.0;
    else if (fraction < 7.5) nice = 5.0;
    else nice = 10.0;

    return nice * base;
}



cv::Mat EchelleTreatment::apply(const cv::Mat& input) const {
    cv::Mat output = input.clone();

    const double targetPixels = output.cols / 6.0;
    const double niceMicrons = niceBarLengthMicrons(targetPixels, calibration_.pixelsPerMicron);
    const int barPixels = static_cast<int>(std::round(niceMicrons * calibration_.pixelsPerMicron));

    const int margin = 24;
    const int y = output.rows - margin;
    const int x1 = margin;
    const int x2 = x1 + barPixels;
    const int tickHalf = 6;

    // Black outline first, white fill on top — keeps the bar legible
    // regardless of what's under it, without needing to inspect the image.
    auto drawBarAndTicks = [&](const cv::Scalar& color, int thickness) {
        cv::line(output, {x1, y}, {x2, y}, color, thickness);
        cv::line(output, {x1, y - tickHalf}, {x1, y + tickHalf}, color, thickness);
        cv::line(output, {x2, y - tickHalf}, {x2, y + tickHalf}, color, thickness);
    };
    drawBarAndTicks(cv::Scalar::all(0), 4);
    drawBarAndTicks(cv::Scalar::all(255), 2);

    const std::string label = formatMicrons(niceMicrons);
    const cv::Point textOrigin(x1, y - tickHalf - 8);
    cv::putText(output, label, textOrigin, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar::all(0), 3);
    cv::putText(output, label, textOrigin, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar::all(255), 1);

    return output;
}

nlohmann::json EchelleTreatment::paramsToJson() const {
    return {{"calibration", calibration_.toJson()}};
}

std::unique_ptr<Treatment> EchelleTreatment::fromJson(const nlohmann::json& params) {
    return std::make_unique<EchelleTreatment>(Calibration::fromJson(params.at("calibration")));
}

} // namespace msapp
