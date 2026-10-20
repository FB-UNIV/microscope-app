#pragma once

#include "treatment.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace msapp {

// Standard black/white point linear stretch: pixels at or below
// blackPoint map to 0, pixels at or above whitePoint map to 255, linear
// in between. Applied identically per-channel.
//
// Note: this is a sane, standard default, not a recovered formula from the
// legacy app — decompiling the old exe for its exact levels math was
// deliberately abandoned in favor of building fresh (see conversation:
// "let's skip the decompiling entirely and start building"). Worth
// comparing output against the old app's output on the same photo once
// real hardware/archive images are available, if exact visual parity
// with old Fiches ever matters.
class LevelsTreatment : public Treatment {
public:
    LevelsTreatment(int blackPoint, int whitePoint);

    std::string type() const override { return "levels"; }
    cv::Mat apply(const cv::Mat& input) const override;
    nlohmann::json paramsToJson() const override;

    static std::unique_ptr<Treatment> fromJson(const nlohmann::json& params);

    int blackPoint() const { return blackPoint_; }
    int whitePoint() const { return whitePoint_; }

private:
    int blackPoint_;
    int whitePoint_;
};

// Every channel value of an 8-bit image (1 or 3 channels) counted: what a
// Levels stretch, the same on each channel, works on. The viewer draws it
// under the black and white handles (#93). Throws std::invalid_argument for
// any other image.
std::array<std::uint64_t, 256> levelsHistogram(const cv::Mat& image);

// The black and white points an Auto button proposes: the lowest level with
// more than `clip` of the values (0.5 % by default) at or below it, and the
// highest with more than `clip` at or above it, so stray dark and bright
// pixels don't hold the stretch back. Always 0 <= black < white <= 255;
// {0, 255} for an empty histogram.
std::pair<int, int> autoLevels(const std::array<std::uint64_t, 256>& histogram, double clip = 0.005);

} // namespace msapp
