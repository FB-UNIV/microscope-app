#include "phase_overlay_treatment.hpp"
#include "phase_analysis.hpp"

#include <stdexcept>
#include <string>

namespace msapp {

PhaseOverlayTreatment::PhaseOverlayTreatment(int threshold) : threshold_(threshold) {
    if (threshold_ < 0 || threshold_ > 255) {
        throw std::invalid_argument("PhaseOverlayTreatment: threshold must be 0..255, got " +
                                    std::to_string(threshold_));
    }
}

cv::Mat PhaseOverlayTreatment::apply(const cv::Mat& input) const {
    // Same grey reduction the phase analysis counts on, so the overlay shows
    // exactly what was measured.
    const cv::Mat grey = legacyGrey(input);

    uchar compressed[256];
    for (int v = 0; v < 256; ++v) {
        compressed[v] = static_cast<uchar>(static_cast<int>(static_cast<float>(v) * 0.9019608f + 25.0f));
    }

    cv::Mat output(grey.rows, grey.cols, CV_8UC3);
    for (int r = 0; r < grey.rows; ++r) {
        const uchar* in = grey.ptr<uchar>(r);
        cv::Vec3b* out = output.ptr<cv::Vec3b>(r);
        for (int c = 0; c < grey.cols; ++c) {
            const uchar level = compressed[in[c]];
            if (in[c] < threshold_) {
                out[c] = cv::Vec3b(level, static_cast<uchar>(level / 2), level);  // B, G, R
            } else {
                out[c] = cv::Vec3b(0, level, level);
            }
        }
    }
    return output;
}

nlohmann::json PhaseOverlayTreatment::paramsToJson() const {
    return {{"threshold", threshold_}};
}

std::unique_ptr<Treatment> PhaseOverlayTreatment::fromJson(const nlohmann::json& params) {
    return std::make_unique<PhaseOverlayTreatment>(params.at("threshold").get<int>());
}

} // namespace msapp
