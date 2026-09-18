#pragma once

#include "treatment.hpp"

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

} // namespace msapp
