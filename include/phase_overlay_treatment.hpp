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
// swap here if teachers remember the old colours the other way round. The
// map was judged right at the first lab session (2026-10-16).
//
// With a zone (#55, analyzePhasesInZone) only that zone is mapped, outlined
// in cyan; the rest of the photo shows as it is. The zone is kept in the
// sidecar, so reopening the Vue analyses the same zone again.
class PhaseOverlayTreatment : public Treatment {
public:
    // Throws std::invalid_argument for a threshold outside 0..255, or a zone
    // that is empty or starts left of or above the image.
    explicit PhaseOverlayTreatment(int threshold, std::optional<cv::Rect> zone = std::nullopt);

    std::string type() const override { return "phase_overlay"; }
    cv::Mat apply(const cv::Mat& input) const override;
    // A zone reaching past the image's edges.
    std::optional<std::string> frameMismatch(int width, int height) const override;
    nlohmann::json paramsToJson() const override;

    // A sidecar without "zone" maps the whole photo, as before #55.
    static std::unique_ptr<Treatment> fromJson(const nlohmann::json& params);

    int threshold() const { return threshold_; }
    const std::optional<cv::Rect>& zone() const { return zone_; }

private:
    int threshold_;
    std::optional<cv::Rect> zone_;
};

} // namespace msapp
