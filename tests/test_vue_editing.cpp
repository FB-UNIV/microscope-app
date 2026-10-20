// User journey:
//   As a student in the Vue viewer, I set the Levels again until the photo
//   reads well, put them before the Échelle so the scale bar keeps its
//   contrast, and undo a Treatment I no longer want — each change saved at
//   once in the sidecar, never touching the stored photo (ADR 0002, 0003).

#include "echelle_treatment.hpp"
#include "image_io.hpp"
#include "levels_treatment.hpp"
#include "phase_overlay_treatment.hpp"
#include "vue.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

int levelsBlack(const Vue& vue, std::size_t index) {
    return dynamic_cast<const LevelsTreatment&>(vue.treatment(index)).blackPoint();
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_vue_editing_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const cv::Mat frame(60, 80, CV_8UC3, cv::Scalar(90, 120, 150));
    const Calibration at80x60{"x40", 2.5, 80, 60};

    std::cout << "== Insert, replace, remove: each saved at once ==\n";
    {
        Vue vue = Vue::capture(frame, dir / "edit.png", std::nullopt, at80x60);
        vue.addTreatment(std::make_unique<EchelleTreatment>(at80x60));
        vue.insertTreatment(0, std::make_unique<LevelsTreatment>(20, 200));
        check(vue.treatmentCount() == 2 && vue.treatment(0).type() == "levels" && vue.treatment(1).type() == "echelle",
              "Levels inserted before the Échelle");

        vue.replaceTreatment(0, std::make_unique<LevelsTreatment>(40, 220));
        check(vue.treatmentCount() == 2 && levelsBlack(vue, 0) == 40, "Levels set again replace the old ones in place");

        vue.insertTreatment(2, std::make_unique<PhaseOverlayTreatment>(127));
        check(vue.treatment(2).type() == "phase_overlay", "inserting at the end appends");
        vue.removeTreatment(2);
        check(vue.treatmentCount() == 2, "a Treatment can be removed (undo)");

        const Vue reloaded = Vue::load(dir / "edit.png");
        check(reloaded.treatmentCount() == 2 && reloaded.treatment(0).type() == "levels" && levelsBlack(reloaded, 0) == 40 &&
                  reloaded.treatment(1).type() == "echelle",
              "the sidecar holds the edited pipeline, in order");
        check(cv::norm(reloaded.original(), frame, cv::NORM_INF) == 0, "the stored photo is untouched");
    }

    std::cout << "\n== Refusals ==\n";
    {
        Vue vue = Vue::capture(frame, dir / "refuse.png");
        vue.addTreatment(std::make_unique<LevelsTreatment>(10, 240));
        check(!throwsWith<std::out_of_range>([&] { vue.removeTreatment(1); }).empty() &&
                  !throwsWith<std::out_of_range>([&] { vue.replaceTreatment(5, std::make_unique<LevelsTreatment>(1, 2)); })
                       .empty() &&
                  !throwsWith<std::out_of_range>([&] { vue.insertTreatment(2, std::make_unique<LevelsTreatment>(1, 2)); })
                       .empty() &&
                  !throwsWith<std::out_of_range>([&] { vue.treatment(1); }).empty(),
              "an index past the pipeline is refused");

        const Calibration at4k{"x40", 2.5, 3840, 2160};
        check(!throwsWith<std::runtime_error>([&] { vue.insertTreatment(0, std::make_unique<EchelleTreatment>(at4k)); })
                   .empty() &&
                  !throwsWith<std::runtime_error>(
                       [&] { vue.replaceTreatment(0, std::make_unique<EchelleTreatment>(at4k)); })
                       .empty(),
              "an Échelle measured at another frame size can't be inserted or swapped in (ADR 0015)");
        check(vue.treatmentCount() == 1 && vue.treatment(0).type() == "levels", "and the pipeline is unchanged");
    }

    std::cout << "\n== A failed save leaves the pipeline as it was ==\n";
    {
        Vue vue = Vue::capture(frame, dir / "fail.png");
        vue.addTreatment(std::make_unique<LevelsTreatment>(10, 240));
        vue.addTreatment(std::make_unique<PhaseOverlayTreatment>(100));
        // A folder where the save writes its temp file makes the next save
        // fail; the failed save clears it away, so set it before each one.
        const auto blockNextSave = [&] { fs::create_directory(vue.sidecarPath().string() + ".tmp"); };

        blockNextSave();
        check(!throwsWith<std::runtime_error>([&] { vue.removeTreatment(0); }).empty() && vue.treatmentCount() == 2 &&
                  vue.treatment(0).type() == "levels",
              "remove: the Treatment is put back");
        blockNextSave();
        check(!throwsWith<std::runtime_error>([&] { vue.replaceTreatment(0, std::make_unique<LevelsTreatment>(50, 60)); })
                       .empty() &&
                  levelsBlack(vue, 0) == 10,
              "replace: the old Treatment is put back");
        blockNextSave();
        check(!throwsWith<std::runtime_error>([&] { vue.insertTreatment(1, std::make_unique<LevelsTreatment>(5, 6)); })
                       .empty() &&
                  vue.treatmentCount() == 2 && vue.treatment(1).type() == "phase_overlay",
              "insert: the new Treatment is taken out again");
        blockNextSave();
        check(!throwsWith<std::runtime_error>([&] { vue.addTreatment(std::make_unique<LevelsTreatment>(5, 6)); })
                       .empty() &&
                  vue.treatmentCount() == 2 && vue.treatment(1).type() == "phase_overlay",
              "add: the new Treatment is taken out again, not shown and saved by the next edit");
        vue.removeTreatment(1);
        const Vue reloaded = Vue::load(dir / "fail.png");
        check(reloaded.treatmentCount() == 1 && reloaded.treatment(0).type() == "levels",
              "the next save holds only what was saved, never the refused Treatment");
    }

    std::cout << "\n== Images are written whole (ADR 0004) ==\n";
    {
        // A folder where the write puts its temp file makes it fail.
        fs::create_directory(dir / "interrupted.png.tmp");
        check(!throwsWith<std::runtime_error>([&] { Vue::capture(frame, dir / "interrupted.png"); }).empty() &&
                  !fs::exists(dir / "interrupted.png"),
              "a capture whose write fails leaves no half photo where the Vue would be");
        fs::remove_all(dir / "interrupted.png.tmp");
        Vue::capture(frame, dir / "interrupted.png");
        check(cv::norm(Vue::load(dir / "interrupted.png").original(), frame, cv::NORM_INF) == 0 &&
                  !fs::exists(dir / "interrupted.png.tmp"),
              "the same capture then succeeds, no temp file left behind");
    }

    std::cout << "\n== Load: an image that isn't 8-bit (review H3) ==\n";
    {
        // A 16-bit TIFF from another program, dropped in an Échantillon folder.
        const cv::Mat deep(60, 80, CV_16UC3, cv::Scalar(0, 32896, 65535));
        writeImage(dir / "deep.tif", deep, {}, "test");
        Vue vue = Vue::load(dir / "deep.tif");
        check(vue.original().type() == CV_8UC3 && vue.original().at<cv::Vec3b>(0, 0) == cv::Vec3b(0, 128, 255),
              "a 16-bit image opens scaled to 8 bits, the full range kept");
        vue.addTreatment(std::make_unique<LevelsTreatment>(20, 200));
        vue.addTreatment(std::make_unique<PhaseOverlayTreatment>(100));
        check(vue.render().type() == CV_8UC3, "its Treatments render instead of throwing");
        check(readImage(dir / "deep.tif", cv::IMREAD_UNCHANGED).depth() == CV_16U, "its file stays as it was");

        const cv::Mat floats(60, 80, CV_32FC1, cv::Scalar(0.5));
        writeImage(dir / "float.tif", floats, {}, "test");
        const std::string why = throwsWith<std::runtime_error>([&] { Vue::load(dir / "float.tif"); });
        check(!why.empty() && why.find("float.tif") != std::string::npos,
              "any other depth is refused when the Vue opens, naming the file");
    }

    std::cout << "\n== Export a flattened copy (#28) ==\n";
    {
        Vue vue = Vue::capture(frame, dir / "export.png");
        vue.addTreatment(std::make_unique<LevelsTreatment>(80, 160));
        const std::string sidecarBefore = [&] {
            std::ifstream in(vue.sidecarPath(), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();

        vue.exportRendered(dir / "copy.png");
        const cv::Mat copy = cv::imread((dir / "copy.png").string(), cv::IMREAD_UNCHANGED);
        check(!copy.empty() && cv::norm(copy, vue.render(), cv::NORM_INF) == 0,
              "a PNG export is the rendered Vue, Treatments applied");
        check(cv::norm(cv::imread((dir / "export.png").string(), cv::IMREAD_UNCHANGED), frame, cv::NORM_INF) == 0,
              "the stored photo is untouched");
        vue.exportRendered(dir / "copy.JPG");
        vue.exportRendered(dir / "copy.tiff");
        check(!cv::imread((dir / "copy.JPG").string()).empty() && !cv::imread((dir / "copy.tiff").string()).empty(),
              "JPEG and TIFF exports are written too");
        check(!fs::exists(dir / "copy.png.tmp") && !fs::exists(dir / "copy.JPG.tmp"), "no temp file left behind");
        fs::create_directory(dir / "blocked.png.tmp");
        check(!throwsWith<std::runtime_error>([&] { vue.exportRendered(dir / "blocked.png"); }).empty() &&
                  !fs::exists(dir / "blocked.png"),
              "a failed export leaves no half image behind");

        check(!throwsWith<std::invalid_argument>([&] { vue.exportRendered(dir / "export.png"); }).empty() &&
                  !throwsWith<std::invalid_argument>([&] { vue.exportRendered(dir / "." / "export.png"); }).empty(),
              "exporting over the Vue's own photo is refused, however the path is spelled");
        check(!throwsWith<std::invalid_argument>([&] { vue.exportRendered(vue.sidecarPath()); }).empty(),
              "exporting over its sidecar is refused");
        check(!throwsWith<std::invalid_argument>([&] { vue.exportRendered(dir / "copy.gif"); }).empty() &&
                  !fs::exists(dir / "copy.gif"),
              "an unsupported format is refused, writing nothing");
        const std::string sidecarAfter = [&] {
            std::ifstream in(vue.sidecarPath(), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();
        check(sidecarAfter == sidecarBefore, "exporting never rewrites the sidecar");
    }

    std::cout << "\n== Undo and redo: a pipeline snapshot put back whole (#93) ==\n";
    {
        Vue vue = Vue::capture(frame, dir / "undo.png", std::nullopt, at80x60);
        vue.addTreatment(std::make_unique<EchelleTreatment>(at80x60));
        const nlohmann::json before = vue.treatmentsJson();
        vue.insertTreatment(0, std::make_unique<LevelsTreatment>(30, 210));
        const nlohmann::json after = vue.treatmentsJson();
        check(before.size() == 1 && after.size() == 2 && after[0]["type"] == "levels",
              "treatmentsJson() is the pipeline as the sidecar records it");

        vue.setTreatments(before);
        check(vue.treatmentCount() == 1 && vue.treatment(0).type() == "echelle", "undo puts the pipeline back");
        check(Vue::load(dir / "undo.png").treatmentCount() == 1, "the undone pipeline is what the sidecar holds");

        vue.setTreatments(after);
        check(vue.treatmentCount() == 2 && levelsBlack(vue, 0) == 30 && Vue::load(dir / "undo.png").treatmentCount() == 2,
              "redo puts the edit back, saved");

        nlohmann::json otherSize = after;
        otherSize.push_back({{"type", "echelle"}, {"params", EchelleTreatment({"x40", 2.5, 160, 120}).paramsToJson()}});
        check(!throwsWith<std::runtime_error>([&] { vue.setTreatments(otherSize); }).empty() && vue.treatmentCount() == 2,
              "a pipeline with an Échelle of another frame size is refused whole");
        nlohmann::json unknown = after;
        unknown.push_back({{"type", "no_such_treatment"}, {"params", nlohmann::json::object()}});
        check(!throwsWith<std::out_of_range>([&] { vue.setTreatments(unknown); }).empty() && vue.treatmentCount() == 2 &&
                  Vue::load(dir / "undo.png").treatmentCount() == 2,
              "an unknown Treatment is refused whole, the sidecar untouched");
    }

    std::cout << "\n== Auto Levels (#93) ==\n";
    {
        cv::Mat ramp(1, 256 * 1000, CV_8UC1);
        for (int i = 0; i < ramp.cols; ++i) ramp.at<uchar>(0, i) = static_cast<uchar>(i / 1000);
        const auto rampLevels = autoLevels(levelsHistogram(ramp));
        check(rampLevels.first == 1 && rampLevels.second == 254,
              "a ramp leaves its 0.5 % darkest and brightest values out of the stretch");

        cv::Mat twoPhases(10, 10, CV_8UC3, cv::Scalar(50, 50, 50));
        twoPhases(cv::Rect(0, 0, 10, 5)).setTo(cv::Scalar(200, 200, 200));
        check(autoLevels(levelsHistogram(twoPhases)) == std::make_pair(50, 200),
              "two phases: the stretch runs from the darker to the lighter");

        std::uint64_t counted = 0;
        for (const std::uint64_t count : levelsHistogram(frame)) counted += count;
        check(counted == 80u * 60u * 3u, "every channel value is counted, as Levels stretch each channel");

        check(autoLevels(levelsHistogram(cv::Mat(8, 8, CV_8UC3, cv::Scalar(120, 120, 120)))) == std::make_pair(120, 121),
              "a flat photo gets a one-level range, never black >= white");
        check(autoLevels(levelsHistogram(cv::Mat(8, 8, CV_8UC1, cv::Scalar(255)))) == std::make_pair(254, 255),
              "a white photo too");
        check(autoLevels(std::array<std::uint64_t, 256>{}) == std::make_pair(0, 255), "no values: no stretch");
        check(!throwsWith<std::invalid_argument>([] { levelsHistogram(cv::Mat(4, 4, CV_16UC1)); }).empty(),
              "a 16-bit image is refused");
    }

    fs::remove_all(dir);
    return summary();
}
