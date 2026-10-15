// User journey:
//   As a student in TP1 Aciers, I capture a steel micrograph and ask the app
//   for "Pourcentage des constituants" — the area fraction of the dark phase
//   (pearlite) vs the light phase (ferrite) — and get the same number the
//   legacy Metal app would have given me (ADR 0012: bit-exact port).
//
// Golden values for the legacy CTTM TIFFs come from
// tools/legacy_phase_reference.py, an independent numpy implementation of
// the same recovered spec. Those checks need the legacy data folder (passed
// as argv[1] or MSAPP_LEGACY_DATA_DIR) and are skipped when it is absent.

#include "phase_analysis.hpp"
#include "phase_overlay_treatment.hpp"
#include "treatment_factory.hpp"
#include "test_support.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

// Dark discs (grey 60) on a light matrix (grey 200), lightly blurred so the
// edges carry the gradient the auto-threshold relies on.
cv::Mat synthesizeTwoPhaseSample() {
    cv::Mat img(400, 600, CV_8UC1, cv::Scalar(200));
    cv::RNG rng(7);
    for (int i = 0; i < 40; ++i) {
        cv::Point center(rng.uniform(0, img.cols), rng.uniform(0, img.rows));
        cv::circle(img, center, rng.uniform(10, 35), cv::Scalar(60), cv::FILLED);
    }
    cv::GaussianBlur(img, img, cv::Size(5, 5), 0);
    return img;
}

fs::path legacyDataDir(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* env = std::getenv("MSAPP_LEGACY_DATA_DIR")) return env;
    return {};
}

struct Golden {
    const char* file;
    int threshold;
    int tolerance;
    double dark;
    double light;
    double error;
};

} // namespace

int main(int argc, char** argv) {
    std::cout << "== legacyGrey: integer (R+G+B)/3, not luminance ==\n";
    {
        cv::Mat bgr(1, 2, CV_8UC3);
        bgr.at<cv::Vec3b>(0, 0) = {10, 20, 31};   // 61 / 3 = 20 (truncated)
        bgr.at<cv::Vec3b>(0, 1) = {0, 255, 0};    // luminance would give ~150, legacy gives 85
        cv::Mat grey = legacyGrey(bgr);
        check(grey.type() == CV_8UC1, "output is single-channel 8-bit");
        check(grey.at<uchar>(0, 0) == 20, "(10+20+31)/3 truncates to 20");
        check(grey.at<uchar>(0, 1) == 85, "pure green maps to 85, not a luminance value");
    }

    std::cout << "\n== countPhases: band pixels split half/half ==\n";
    {
        // threshold 100, tolerance 10 -> band [90, 110], light > 110.
        cv::Mat grey = (cv::Mat_<uchar>(1, 10) << 0, 50, 89, 90, 100, 110, 111, 200, 255, 255);
        PhaseFractions f = countPhases(grey, 100, 10);
        // high = 4 (111, 200, 255, 255), band = 3 -> band/2 = 1 (integer)
        check(nearlyEqual(f.lightPercent, 50.0, 1e-5), "light = (high + band/2)/N = 5/10 = 50% (got " +
                                                          std::to_string(f.lightPercent) + ")");
        check(nearlyEqual(f.darkPercent, 50.0, 1e-5), "dark = (N - high - band/2)/N = 5/10 = 50%");
        check(nearlyEqual(f.errorPercent, 15.0, 1e-5), "error = band/(2N) = 3/20 = 15%");
    }

    std::cout << "\n== computeAutoThreshold: separates a synthetic two-phase sample ==\n";
    {
        cv::Mat sample = synthesizeTwoPhaseSample();
        AutoThreshold t = computeAutoThreshold(sample);
        check(t.threshold > 60 && t.threshold < 200,
              "threshold lands between the two phases (got " + std::to_string(t.threshold) + ")");
        check(t.tolerance >= 0 && t.tolerance < 60, "tolerance is a modest band (got " +
                                                       std::to_string(t.tolerance) + ")");

        const double trueDark = 100.0 * cv::countNonZero(sample < 130) / static_cast<double>(sample.total());
        PhaseAnalysis a = analyzePhases(sample);
        check(nearlyEqual(a.fractions.darkPercent + a.fractions.lightPercent, 100.0, 1e-3),
              "dark + light = 100%");
        check(nearlyEqual(a.fractions.darkPercent, trueDark, 2.0),
              "dark fraction within 2 points of the drawn disc area (got " +
                  std::to_string(a.fractions.darkPercent) + " vs " + std::to_string(trueDark) + ")");
    }

    std::cout << "\n== legacySmooth: 5x5 disk opening+closing, replicated 2px frame ==\n";
    {
        cv::Mat grey(20, 20, CV_8UC1, cv::Scalar(100));
        grey.at<uchar>(10, 10) = 255;  // isolated bright speck
        cv::Mat smoothed = legacySmooth(grey);
        check(smoothed.at<uchar>(10, 10) == 100, "opening removes an isolated 1-pixel speck");

        cv::Mat ramp(12, 12, CV_8UC1);
        for (int r = 0; r < ramp.rows; ++r)
            for (int c = 0; c < ramp.cols; ++c) ramp.at<uchar>(r, c) = static_cast<uchar>(r * 10 + c);
        cv::Mat out = legacySmooth(ramp);
        bool frameReplicated = true;
        for (int c = 0; c < out.cols; ++c) {
            frameReplicated &= out.at<uchar>(0, c) == out.at<uchar>(2, c);
            frameReplicated &= out.at<uchar>(out.rows - 1, c) == out.at<uchar>(out.rows - 3, c);
        }
        for (int r = 0; r < out.rows; ++r) {
            frameReplicated &= out.at<uchar>(r, 0) == out.at<uchar>(r, 2);
            frameReplicated &= out.at<uchar>(r, out.cols - 1) == out.at<uchar>(r, out.cols - 3);
        }
        check(frameReplicated, "outer 2 rows/cols are copies of the nearest computed row/col");
    }

    std::cout << "\n== invalid input is an error, not a silent wrong answer ==\n";
    {
        bool threw = false;
        try {
            computeAutoThreshold(cv::Mat(10, 10, CV_16UC1, cv::Scalar(0)));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "16-bit input throws invalid_argument");
        threw = false;
        try {
            legacySmooth(cv::Mat(4, 4, CV_8UC1, cv::Scalar(0)));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "image smaller than the 5x5 kernel throws invalid_argument");
    }

    std::cout << "\n== PhaseOverlayTreatment: two-colour phase map ==\n";
    {
        cv::Mat grey = (cv::Mat_<uchar>(1, 2) << 50, 150);
        PhaseOverlayTreatment overlay(100);
        cv::Mat out = overlay.apply(grey);
        const cv::Vec3b below = out.at<cv::Vec3b>(0, 0);
        const cv::Vec3b above = out.at<cv::Vec3b>(0, 1);
        // 50 -> 50*0.9019608+25 = 70 ; 150 -> 160
        check(below == cv::Vec3b(70, 35, 70), "below threshold -> magenta (L, L/2, L) as RGB");
        check(above == cv::Vec3b(0, 160, 160), "at/above threshold -> yellow (L, L, 0) as RGB");

        auto rebuilt = TreatmentFactory::instance().create("phase_overlay", overlay.paramsToJson());
        cv::Mat diff;
        cv::absdiff(rebuilt->apply(grey), out, diff);
        check(cv::countNonZero(diff.reshape(1)) == 0, "sidecar round-trip rebuilds an identical overlay");
    }

    // Always-on parity: 256x256 centre crops of three legacy views (low, mid
    // and high dark fraction), stored losslessly in the repo so CI checks the
    // port without the 15 MB archive. Golden values from
    // tools/legacy_phase_reference.py run on these exact PNGs.
    std::cout << "\n== Legacy parity: in-repo crops vs independent numpy reference ==\n";
    {
        const fs::path fixtures = fs::path(MSAPP_SOURCE_DIR) / "tests" / "fixtures" / "legacy";
        const Golden crops[] = {
            {"CTTM1Ech1Vue1_crop256.png", 95, 9, 13.282776, 86.717224, 1.599884},
            {"CTTM1Ech2Vue1_crop256.png", 150, 13, 64.813232, 35.186768, 5.811310},
            {"CTTM1Ech3Vue1_crop256.png", 161, 12, 91.578674, 8.421326, 1.897430},
        };
        for (const Golden& g : crops) {
            cv::Mat img = cv::imread((fixtures / g.file).string(), cv::IMREAD_UNCHANGED);
            if (img.empty()) {
                check(false, std::string("could not read fixture ") + g.file);
                continue;
            }
            PhaseAnalysis a = analyzePhases(img);
            const std::string got = " (got t=" + std::to_string(a.threshold.threshold) + " tol=" +
                                    std::to_string(a.threshold.tolerance) + " dark=" +
                                    std::to_string(a.fractions.darkPercent) + ")";
            check(a.threshold.threshold == g.threshold && a.threshold.tolerance == g.tolerance &&
                      nearlyEqual(a.fractions.darkPercent, g.dark, 1e-5) &&
                      nearlyEqual(a.fractions.lightPercent, g.light, 1e-5) &&
                      nearlyEqual(a.fractions.errorPercent, g.error, 1e-5),
                  std::string(g.file) + " matches reference" + got);
        }
    }

    std::cout << "\n== Legacy parity: CTTM TIFFs vs independent numpy reference ==\n";
    const fs::path dataDir = legacyDataDir(argc, argv);
    if (dataDir.empty() || !fs::exists(dataDir / "CTTM1Ech1Vue1.tif")) {
        std::cout << "  [SKIP] legacy data folder not given (argv[1] or MSAPP_LEGACY_DATA_DIR)\n";
    } else {
        const Golden goldens[] = {
            {"CTTM1Ech1Vue1.tif", 96, 9, 13.383953, 86.616051, 1.838166},
            {"CTTM1Ech1Vue2.tif", 103, 8, 19.050827, 80.949173, 3.518068},
            {"CTTM1Ech2Vue1.tif", 144, 14, 64.468086, 35.531914, 5.843189},
            {"CTTM1Ech3Vue1.tif", 165, 12, 86.096848, 13.903151, 3.007627},
            {"CTTM1Ech4Vue2.tif", 163, 13, 86.226562, 13.773440, 2.132580},
        };
        for (const Golden& g : goldens) {
            cv::Mat img = cv::imread((dataDir / g.file).string(), cv::IMREAD_UNCHANGED);
            if (img.empty()) {
                check(false, std::string("could not read ") + g.file);
                continue;
            }
            PhaseAnalysis a = analyzePhases(img);
            const std::string got = " (got t=" + std::to_string(a.threshold.threshold) + " tol=" +
                                    std::to_string(a.threshold.tolerance) + " dark=" +
                                    std::to_string(a.fractions.darkPercent) + ")";
            check(a.threshold.threshold == g.threshold && a.threshold.tolerance == g.tolerance &&
                      nearlyEqual(a.fractions.darkPercent, g.dark, 1e-5) &&
                      nearlyEqual(a.fractions.lightPercent, g.light, 1e-5) &&
                      nearlyEqual(a.fractions.errorPercent, g.error, 1e-5),
                  std::string(g.file) + " matches reference" + got);
        }
    }

    return summary();
}
