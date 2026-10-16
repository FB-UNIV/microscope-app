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

// Phase analysis on a zone of the Vue (#55): samples often carry dust,
// inclusions or scratches that would count as a phase, so the operator can
// leave them out — as long as the zone still holds enough grains to mean
// something. The smallest zone accepted, as a share of the image area. A
// placeholder until the teacher gives a figure.
constexpr double kMinPhaseZoneFraction = 0.15;

// Why `zone` can't be analysed on an image of `size`: not inside it, or
// smaller than kMinPhaseZoneFraction of it. Empty when it can.
std::optional<std::string> phaseZoneProblem(const cv::Rect& zone, const cv::Size& size);

// analyzePhases() of `zone` alone: the threshold is found and the phases
// counted on that crop, exactly as if it were the whole photo. A zone
// covering the whole image gives analyzePhases(image), still bit-exact with
// the legacy app (ADR 0012). Throws std::invalid_argument with
// phaseZoneProblem()'s reason.
PhaseAnalysis analyzePhasesInZone(const cv::Mat& image, const cv::Rect& zone);

} // namespace msapp
