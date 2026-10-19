#pragma once

#include <opencv2/core.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace msapp {

// Phase analysis (see CONTEXT.md, legacy "Pourcentage des constituants"):
// splits a micrograph into a dark and a light phase (e.g. pearlite vs
// ferrite in a steel) and reports each phase's area percentage.
//
// This is a bit-exact port of the legacy Metal app's algorithm, recovered
// by decompiling MetalNew-wx3 (see ADR 0012 for why we port rather than
// redesign, and docs/legacy/reverse-engineering-report.md §5 for
// the addresses). Every float->int step truncates, as the legacy x87 code
// did; "simplifying" any of these constants changes the numbers students
// get compared to years of archived results.

// Result of the legacy automatic threshold (denombre_metal_ima_fast).
struct AutoThreshold {
    int threshold = 127;   // grey level separating the two phases
    int tolerance = 42;    // half-width of the uncertain band around threshold
    // Edge-weighted grey histogram after smoothing, with the threshold bin
    // scaled by 1/sqrt(2) as a visual marker — what the legacy histogram
    // window displayed.
    std::array<std::uint32_t, 256> histogram{};
    // False when no edge stood out, so the histogram is empty: threshold and
    // tolerance are then the legacy's defaults (127 ± 42) and mean nothing.
    // The legacy app showed its numbers anyway; callers should not.
    bool found = true;
};

// Phase area percentages (legacy denombre). Pixels inside
// [threshold - tolerance, threshold + tolerance] count half to each phase;
// errorPercent is that band's share, i.e. the measurement uncertainty.
struct PhaseFractions {
    double darkPercent = 0.0;
    double lightPercent = 0.0;
    double errorPercent = 0.0;
};

struct PhaseAnalysis {
    AutoThreshold threshold;
    PhaseFractions fractions;
    cv::Mat analysedGrey;  // the 8-bit image the fractions were counted on
};

// Legacy colour -> grey conversion: integer (R + G + B) / 3, not luminance.
cv::Mat legacyGrey(const cv::Mat& bgr);

// Edge-weighted histogram threshold. Expects CV_8UC1, at least 3x3.
AutoThreshold computeAutoThreshold(const cv::Mat& grey);

// Counts every pixel of a CV_8UC1 image against threshold +/- tolerance.
PhaseFractions countPhases(const cv::Mat& grey, int threshold, int tolerance);

// Grey morphological opening then closing with a 5x5 disk (square minus
// corners), legacy border handling (2-pixel border replicated from the
// computed interior). Applied to colour captures before counting.
cv::Mat legacySmooth(const cv::Mat& grey);

// Full legacy pipeline (gere_calcul): grey input is thresholded and counted
// as-is; colour input is converted with legacyGrey, thresholded, smoothed
// with legacySmooth, then counted on the smoothed image.
PhaseAnalysis analyzePhases(const cv::Mat& image);

// The frame width the legacy algorithm was tuned on: the old camera's
// 1042x780. Its edge detector compares neighbouring pixels, which a 4K
// frame, soft at the pixel level, defeats (ADR 0021).
constexpr int kLegacyFrameWidth = 1042;

// The smallest side, in pixels of an image of `size`, of a zone that can be
// analysed: 5 pixels at the legacy scale, the size of legacySmooth's disk
// (5 on a legacy-sized image, 19 on a 4K one).
int minPhaseZoneSide(const cv::Size& size);

// Why `zone` can't be analysed on an image of `size`: not inside it, or a
// side shorter than minPhaseZoneSide(). Empty when it can. There is no rule
// on how much of the image a zone covers (#55): a share of the image says
// nothing about how many grains it holds; the teacher decides (#64).
std::optional<std::string> phaseZoneProblem(const cv::Rect& zone, const cv::Size& size);

// Phase analysis of a Vue (#55, ADR 0021): analyzePhases() of `zone`, or of
// the whole image without one — samples often carry dust, inclusions or
// scratches the operator can leave out. An image wider than
// kLegacyFrameWidth is first scaled down to that width, and a zone by the
// same factor as its frame, so the legacy algorithm sees edges as sharp as
// on the old camera. An image no wider is analysed as it is, bit-exact with
// the legacy app (ADR 0012). Check threshold.found before showing the
// result. Throws std::invalid_argument with phaseZoneProblem()'s reason.
PhaseAnalysis analyzeVuePhases(const cv::Mat& image, std::optional<cv::Rect> zone = std::nullopt);

} // namespace msapp
