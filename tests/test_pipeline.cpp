#include "vue.hpp"
#include "levels_treatment.hpp"
#include "echelle_treatment.hpp"
#include "calibration.hpp"
#include "test_support.hpp"

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

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

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::set<std::string> entryNames(const fs::path& dir) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir)) names.insert(entry.path().filename().string());
    return names;
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

    std::cout << "\n== EchelleTreatment: a Calibration without a positive px/um is rejected ==\n";
    {
        auto rejects = [](double pixelsPerMicron) {
            try {
                EchelleTreatment echelle(Calibration{"x40", pixelsPerMicron});
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
        check(rejects(0.0), "0 px/um throws invalid_argument");
        check(rejects(-2.5), "negative px/um throws invalid_argument");
        check(rejects(std::numeric_limits<double>::quiet_NaN()), "NaN px/um throws invalid_argument");
        check(rejects(std::numeric_limits<double>::infinity()), "infinite px/um throws invalid_argument");
        check(!rejects(2.5), "2.5 px/um is accepted");

        bool threw = false;
        try {
            EchelleTreatment::fromJson({{"calibration", {{"grossissement", "x40"}, {"pixels_per_micron", 0}}}});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "fromJson rejects a sidecar Échelle with pixels_per_micron 0");
    }

    std::cout << "\n== Full pipeline: capture -> treat -> render -> save -> reload -> re-render ==\n";
    {
        const std::set<std::string> workingDirBefore = entryNames(fs::current_path());
        // Cleared at the start, kept afterwards so the PNGs can be inspected.
        const fs::path outDir = fs::temp_directory_path() / "msapp_pipeline_test";
        fs::remove_all(outDir);
        fs::create_directories(outDir);
        fs::path imagePath = outDir / "vue_001.tiff";

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

        check(entryNames(fs::current_path()) == workingDirBefore,
              "nothing was written to the working directory (output goes under the temp directory)");
    }

    std::cout << "\n== Sidecar safety: atomic saves, an unreadable sidecar never costs the photo ==\n";
    {
        const fs::path dir = fs::temp_directory_path() / "msapp_vue_sidecar_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const cv::Mat sample = synthesizeTestSample();

        Vue vue = Vue::capture(sample, dir / "atomic.png");
        vue.addTreatment(std::make_unique<LevelsTreatment>(90, 180));
        const std::string savedSidecar = readFile(vue.sidecarPath());
        const fs::path tempPath = vue.sidecarPath().string() + ".tmp";
        check(!fs::exists(tempPath), "a save leaves no temp file behind");

        fs::create_directory(tempPath);  // the next save can no longer write its temp file
        bool threw = false;
        try {
            vue.addTreatment(std::make_unique<EchelleTreatment>(Calibration{"x40", 2.5}));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "a save that cannot be written throws");
        check(readFile(vue.sidecarPath()) == savedSidecar, "a failed save leaves the previous sidecar intact");

        const fs::path corruptImage = dir / "corrupt.png";
        cv::imwrite(corruptImage.string(), sample);
        const fs::path corruptSidecar = corruptImage.string() + ".json";
        const std::string truncated = R"({"treatments": [{"type": "levels", "params": {"black_po)";
        std::ofstream(corruptSidecar, std::ios::binary) << truncated;

        Vue bare;
        try {
            bare = Vue::load(corruptImage);
            check(true, "a truncated sidecar does not stop the photo from loading");
        } catch (const std::exception& e) {
            check(false, std::string("a truncated sidecar does not stop the photo from loading (threw: ") +
                             e.what() + ")");
        }
        check(bare.treatmentCount() == 0 && !bare.original().empty(), "it loads as the bare photo, no treatments");
        check(bare.sidecarError().has_value(), "the reason is reported through sidecarError()");
        check(readFile(corruptSidecar) == truncated, "loading writes nothing: the unreadable sidecar is untouched");

        if (bare.sidecarError()) {
            bare.addTreatment(std::make_unique<LevelsTreatment>(90, 180));
            const fs::path aside = corruptSidecar.string() + ".unreadable";
            check(fs::exists(aside) && readFile(aside) == truncated,
                  "the next save keeps the unreadable sidecar aside instead of overwriting it");
            Vue reloaded = Vue::load(corruptImage);
            check(reloaded.treatmentCount() == 1 && !reloaded.sidecarError(), "the new sidecar reloads cleanly");
            bare.addTreatment(std::make_unique<EchelleTreatment>(Calibration{"x40", 2.5}));
            check(!fs::exists(aside.string() + ".2") && readFile(aside) == truncated,
                  "later saves only replace the new sidecar");
        }

        const fs::path unknownImage = dir / "unknown_type.png";
        cv::imwrite(unknownImage.string(), sample);
        std::ofstream(unknownImage.string() + ".json")
            << R"({"treatments": [{"type": "levels", "params": {"black_point": 90, "white_point": 180}},)"
               R"( {"type": "no_such_treatment", "params": {}}]})";
        try {
            Vue partial = Vue::load(unknownImage);
            check(partial.treatmentCount() == 0 && partial.sidecarError() &&
                      partial.sidecarError()->find("no_such_treatment") != std::string::npos,
                  "an unknown treatment type loads none of the stack, not half of it, and is named");
        } catch (const std::exception& e) {
            check(false, std::string("an unknown treatment type does not stop the photo from loading (threw: ") +
                             e.what() + ")");
        }

        const fs::path zeroScaleImage = dir / "zero_scale.png";
        cv::imwrite(zeroScaleImage.string(), sample);
        std::ofstream(zeroScaleImage.string() + ".json")
            << R"({"treatments": [{"type": "echelle", "params":)"
               R"( {"calibration": {"grossissement": "x40", "pixels_per_micron": 0}}}]})";
        try {
            Vue zeroScale = Vue::load(zeroScaleImage);
            check(zeroScale.treatmentCount() == 0 && zeroScale.sidecarError(),
                  "a sidecar Échelle with 0 px/um is reported, never drawn");
        } catch (const std::exception& e) {
            check(false, std::string("a sidecar Échelle with 0 px/um does not stop the photo from loading (threw: ") +
                             e.what() + ")");
        }
        fs::remove_all(dir);
    }

    return summary();
}
