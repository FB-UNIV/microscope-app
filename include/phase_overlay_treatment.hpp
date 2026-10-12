#pragma once

#include "treatment.hpp"

namespace msapp {

// Two-colour phase map (legacy creeImageSeuil, 0x466860): shows which pixels
// fall on each side of a phase-analysis threshold, so the operator can see
// what was counted. Grey level v is first compressed to v*0.902+25 (keeps
// black visible), then pixels below the threshold are tinted magenta
// (L, L/2, L) and pixels at/above it yellow (L, L, 0) as (R, G, B).
//
// The R/G/B plane order is inferred (legacy wrote three planar buffers);
// swap here if teachers remember the old colours the other way round.
class PhaseOverlayTreatment : public Treatment {
public:
    explicit PhaseOverlayTreatment(int threshold);

    std::string type() const override { return "phase_overlay"; }
    cv::Mat apply(const cv::Mat& input) const override;
    nlohmann::json paramsToJson() const override;

    static std::unique_ptr<Treatment> fromJson(const nlohmann::json& params);

    int threshold() const { return threshold_; }

private:
    int threshold_;
};

} // namespace msapp
