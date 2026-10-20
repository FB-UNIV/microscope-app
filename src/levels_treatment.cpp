#include "levels_treatment.hpp"
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

std::array<std::uint64_t, 256> levelsHistogram(const cv::Mat& image) {
    if (image.depth() != CV_8U || (image.channels() != 1 && image.channels() != 3)) {
        throw std::invalid_argument("levelsHistogram: expected an 8-bit image with 1 or 3 channels");
    }
    std::array<std::uint64_t, 256> counts{};
    const int values = image.cols * image.channels();
    for (int y = 0; y < image.rows; ++y) {
        const uchar* row = image.ptr<uchar>(y);
        for (int x = 0; x < values; ++x) ++counts[row[x]];
    }
    return counts;
}

std::pair<int, int> autoLevels(const std::array<std::uint64_t, 256>& histogram, double clip) {
    std::uint64_t total = 0;
    for (const std::uint64_t count : histogram) total += count;
    if (total == 0) return {0, 255};
    const double cut = clip * static_cast<double>(total);
    int black = 0;
    std::uint64_t below = 0;
    for (int level = 0; level < 256; ++level) {
        below += histogram[level];
        if (static_cast<double>(below) > cut) {
            black = level;
            break;
        }
    }
    int white = 255;
    std::uint64_t above = 0;
    for (int level = 255; level >= 0; --level) {
        above += histogram[level];
        if (static_cast<double>(above) > cut) {
            white = level;
            break;
        }
    }
    // A flat photo: one level of range, which LevelsTreatment accepts.
    if (black >= white) {
        if (black >= 255) {
            black = 254;
            white = 255;
        } else {
            white = black + 1;
        }
    }
    return {black, white};
}

} // namespace msapp
