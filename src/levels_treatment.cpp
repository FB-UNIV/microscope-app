#include "levels_treatment.hpp"
#include "treatment_factory.hpp"
#include <algorithm>
#include <stdexcept>

namespace msapp {

LevelsTreatment::LevelsTreatment(int blackPoint, int whitePoint)
    : blackPoint_(blackPoint), whitePoint_(whitePoint) {
    if (blackPoint_ < 0 || whitePoint_ > 255 || blackPoint_ >= whitePoint_) {
        throw std::invalid_argument(
            "LevelsTreatment: expected 0 <= blackPoint < whitePoint <= 255, got " +
            std::to_string(blackPoint_) + ".." + std::to_string(whitePoint_));
    }
}

cv::Mat LevelsTreatment::apply(const cv::Mat& input) const {
    // Build a 256-entry lookup table once, apply via cv::LUT — same
    // approach regardless of channel count (grayscale or BGR), since the
    // stretch is identical per-channel.
    cv::Mat lut(1, 256, CV_8U);
    uchar* lutData = lut.ptr();
    const double range = static_cast<double>(whitePoint_ - blackPoint_);
    for (int i = 0; i < 256; ++i) {
        double stretched = (static_cast<double>(i) - blackPoint_) / range * 255.0;
        lutData[i] = static_cast<uchar>(std::clamp(stretched, 0.0, 255.0));
    }

    cv::Mat output;
    cv::LUT(input, lut, output);
    return output;
}

nlohmann::json LevelsTreatment::paramsToJson() const {
    return {{"black_point", blackPoint_}, {"white_point", whitePoint_}};
}

std::unique_ptr<Treatment> LevelsTreatment::fromJson(const nlohmann::json& params) {
    return std::make_unique<LevelsTreatment>(params.at("black_point").get<int>(),
                                              params.at("white_point").get<int>());
}

namespace {
const bool kRegistered = [] {
    TreatmentFactory::instance().registerType("levels", &LevelsTreatment::fromJson);
    return true;
}();
} // namespace

} // namespace msapp
