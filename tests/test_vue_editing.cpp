// User journey:
//   As a student in the Vue viewer, I set the Levels again until the photo
//   reads well, put them before the Échelle so the scale bar keeps its
//   contrast, and undo a Treatment I no longer want — each change saved at
//   once in the sidecar, never touching the stored photo (ADR 0002, 0003).

#include "echelle_treatment.hpp"
#include "levels_treatment.hpp"
#include "phase_overlay_treatment.hpp"
#include "vue.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

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
        check(!fs::exists(dir / "copy.tmp.png") && !fs::exists(dir / "copy.tmp.JPG"), "no temp file left behind");

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

    fs::remove_all(dir);
    return summary();
}
