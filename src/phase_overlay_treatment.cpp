#include "phase_overlay_treatment.hpp"
#include "phase_analysis.hpp"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace msapp {

namespace {

// The two-colour map of a whole image.
cv::Mat phaseMap(const cv::Mat& input, int threshold) {
    // Same grey reduction the phase analysis thresholds. For colour input the
    // counts are taken on the smoothed grey (legacySmooth) while this overlay
    // is not smoothed, exactly as the legacy creeImageSeuil did.
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
            if (in[c] < threshold) {
                out[c] = cv::Vec3b(level, static_cast<uchar>(level / 2), level);  // B, G, R
            } else {
                out[c] = cv::Vec3b(0, level, level);
            }
        }
    }
    return output;
}

// `input` as 8-bit BGR, the format the map is drawn in.
cv::Mat asBgr(const cv::Mat& input) {
    cv::Mat bgr;
    if (input.type() == CV_8UC1) {
        cv::cvtColor(input, bgr, cv::COLOR_GRAY2BGR);
    } else if (input.type() == CV_8UC4) {
        cv::cvtColor(input, bgr, cv::COLOR_BGRA2BGR);
    } else {
        bgr = input.clone();
    }
    return bgr;
}

std::string describe(const cv::Rect& zone) {
    return std::to_string(zone.width) + "x" + std::to_string(zone.height) + " at (" + std::to_string(zone.x) + ", " +
           std::to_string(zone.y) + ")";
}

} // namespace

PhaseOverlayTreatment::PhaseOverlayTreatment(int threshold, std::optional<cv::Rect> zone)
    : threshold_(threshold), zone_(zone) {
    if (threshold_ < 0 || threshold_ > 255) {
        throw std::invalid_argument("PhaseOverlayTreatment: threshold must be 0..255, got " +
                                    std::to_string(threshold_));
    }
    if (zone_ && (zone_->width <= 0 || zone_->height <= 0 || zone_->x < 0 || zone_->y < 0)) {
        throw std::invalid_argument("PhaseOverlayTreatment: the zone " + describe(*zone_) +
                                    " is empty or starts outside the image");
    }
}

std::optional<std::string> PhaseOverlayTreatment::frameMismatch(int width, int height) const {
    if (!zone_ || (zone_->x + zone_->width <= width && zone_->y + zone_->height <= height)) return std::nullopt;
    return "the phase map zone " + describe(*zone_) + " reaches past the " + std::to_string(width) + "x" +
           std::to_string(height) + " image";
}

cv::Mat PhaseOverlayTreatment::apply(const cv::Mat& input) const {
    if (!zone_) return phaseMap(input, threshold_);
    if (auto why = frameMismatch(input.cols, input.rows)) {
        throw std::invalid_argument("PhaseOverlayTreatment: " + *why);
    }
    cv::Mat output = asBgr(input);
    phaseMap(input(*zone_), threshold_).copyTo(output(*zone_));
    // Shows which part was counted; drawn inside the zone so the photo
    // around it stays as it is.
    const int thickness = std::max(2, std::min(output.cols, output.rows) / 400);
    cv::rectangle(output, *zone_, cv::Scalar(255, 255, 0), thickness);  // cyan (B, G, R)
    return output;
}

nlohmann::json PhaseOverlayTreatment::paramsToJson() const {
    nlohmann::json params = {{"threshold", threshold_}};
    if (zone_) {
        params["zone"] = {{"x", zone_->x}, {"y", zone_->y}, {"width", zone_->width}, {"height", zone_->height}};
    }
    return params;
}

std::unique_ptr<Treatment> PhaseOverlayTreatment::fromJson(const nlohmann::json& params) {
    std::optional<cv::Rect> zone;
    if (params.contains("zone")) {
        const nlohmann::json& z = params.at("zone");
        zone = cv::Rect(z.at("x").get<int>(), z.at("y").get<int>(), z.at("width").get<int>(), z.at("height").get<int>());
    }
    return std::make_unique<PhaseOverlayTreatment>(params.at("threshold").get<int>(), zone);
}

} // namespace msapp
