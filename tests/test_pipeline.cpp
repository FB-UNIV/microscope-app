#include "vue.hpp"
#include "levels_treatment.hpp"
#include "echelle_treatment.hpp"
#include "calibration.hpp"

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace msapp;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  [PASS] " << description << "\n";
    } else {
        std::cout << "  [FAIL] " << description << "\n";
        ++g_failures;
    }
}

// Synthesizes a deliberately low-contrast grayscale test image, standing in
// for a real Moticam capture until real hardware is on hand (Phase 1 scope
// per ADR 0009 — prove the math now, wire the real camera + Qt UI next).
cv::Mat synthesizeTestSample() {
    cv::Mat img(480, 640, CV_8UC1, cv::Scalar(120));
    cv::RNG rng(42);
    // A few irregular "grain boundary" blobs in a narrow mid-gray band —
    // exactly the kind of low-contrast image Levels is meant to fix.
    for (int i = 0; i < 25; ++i) {
        cv::Point center(rng.uniform(0, img.cols), rng.uniform(0, img.rows));
        int radius = rng.uniform(15, 60);
        int gray = rng.uniform(95, 150);
        cv::circle(img, center, radius, cv::Scalar(gray), cv::FILLED);
    }
    cv::GaussianBlur(img, img, cv::Size(9, 9), 0);
    return img;
}

} // namespace

int main() {
    std::cout << "== LevelsTreatment: direct math check ==\n";
    {
        cv::Mat probe(1, 3, CV_8UC1);
        probe.at<uchar>(0, 0) = 90;   // == blackPoint -> should map to 0
        probe.at<uchar>(0, 1) = 135;  // midpoint -> should map to ~127
        probe.at<uchar>(0, 2) = 180;  // == whitePoint -> should map to 255

        LevelsTreatment levels(90, 180);
        cv::Mat out = levels.apply(probe);

        check(out.at<uchar>(0, 0) == 0, "black point (90) maps to 0");
        check(std::abs(static_cast<int>(out.at<uchar>(0, 1)) - 127) <= 1, "midpoint (135) maps to ~127");
        check(out.at<uchar>(0, 2) == 255, "white point (180) maps to 255");
    }

    std::cout << "\n== EchelleTreatment: nice-number bar-length check ==\n";
    {
        // 100 target px at 2.5 px/um -> 40um target -> nearest nice number is 50um.
        double nice = EchelleTreatment::niceBarLengthMicrons(100.0, 2.5);
        check(nice == 50.0, "niceBarLengthMicrons(100px, 2.5px/um) == 50um (got " + std::to_string(nice) + ")");
    }

    std::cout << "\n== Full pipeline: capture -> treat -> render -> save -> reload -> re-render ==\n";
    {
        fs::path outDir = "tests/output";
        fs::create_directories(outDir);
        fs::path imagePath = outDir / "vue_001.tiff";
        fs::path sidecarPath = imagePath.string() + ".json";
        if (fs::exists(imagePath)) fs::remove(imagePath);
        if (fs::exists(sidecarPath)) fs::remove(sidecarPath);

        cv::Mat sample = synthesizeTestSample();
        cv::imwrite((outDir / "00_original_as_captured.png").string(), sample);

        Vue vue = Vue::capture(sample, imagePath);
        check(fs::exists(imagePath), "Vue::capture wrote the original file to disk");
        check(vue.treatmentCount() == 0, "freshly captured Vue has zero treatments");

        Calibration calib{"x40", 2.5}; // px/um — placeholder until real mire calibration
        vue.addTreatment(std::make_unique<LevelsTreatment>(90, 180));
        vue.addTreatment(std::make_unique<EchelleTreatment>(calib));
        check(fs::exists(vue.sidecarPath()), "sidecar JSON was written after addTreatment");

        cv::Mat rendered = vue.render();
        cv::imwrite((outDir / "01_rendered_levels_plus_echelle.png").string(), rendered);

        // Original on disk must be untouched (ADR 0003) — re-read it and
        // compare against what was originally captured, byte for byte.
        cv::Mat rereadOriginal = cv::imread(imagePath.string(), cv::IMREAD_UNCHANGED);
        cv::Mat diffOriginal;
        cv::absdiff(sample, rereadOriginal, diffOriginal);
        check(cv::countNonZero(diffOriginal) == 0,
              "original file on disk is byte-identical to what was captured (non-destructive)");

        // Round-trip: reload the Vue from disk (image + sidecar), re-render,
        // and confirm it's pixel-identical to the first render.
        Vue reloaded = Vue::load(imagePath);
        check(reloaded.treatmentCount() == 2, "reloaded Vue reconstructed both treatments from sidecar");

        cv::Mat reRendered = reloaded.render();
        cv::Mat diffRender;
        cv::absdiff(rendered, reRendered, diffRender);
        check(cv::countNonZero(diffRender) == 0, "round-tripped render is pixel-identical to the original render");

        std::cout << "\n  Sidecar contents (" << vue.sidecarPath() << "):\n";
        std::ifstream sidecarFile(vue.sidecarPath());
        std::cout << sidecarFile.rdbuf() << "\n";

        std::cout << "  Wrote: " << (outDir / "00_original_as_captured.png") << "\n";
        std::cout << "  Wrote: " << (outDir / "01_rendered_levels_plus_echelle.png") << "\n";
    }

    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
