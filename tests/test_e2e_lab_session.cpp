// End-to-end journey (the e2e pipeline's core stage, no Qt, no camera):
//   A teacher sets up a fresh workstation — White reference on a blank
//   field, x10 calibrated on the mire — then a student captures an
//   Échantillon at x40, reopens the Vue in a later session, measures it,
//   runs the phase analysis, flags it for the TP's Fiche, files a stray
//   Unsorted Vue and deletes one to the Corbeille.
//
// Every step goes through the real files the app reads and writes
// (workstation.json, the catalog, the Vue folders, each Vue's sidecar), and
// the camera is the FileReplayCamera the app's --replay option uses. Unit
// tests cover each module; this one checks they still fit together: what
// one step saves is what the next one loads.

#include "capture_station.hpp"
#include "catalog.hpp"
#include "fiche.hpp"
#include "levels_treatment.hpp"
#include "measurement.hpp"
#include "mire_calibration.hpp"
#include "phase_analysis.hpp"
#include "phase_overlay_treatment.hpp"
#include "vickers.hpp"
#include "vue.hpp"
#include "vue_store.hpp"
#include "white_reference.hpp"
#include "workstation_config.hpp"
#include "test_support.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

// Small frames keep the test fast; every replayed frame has the same size,
// like a camera's, so a Calibration measured on one holds for the others.
const cv::Size kFrame(768, 512);
const cv::Scalar kTint(200, 120, 180);  // green-deficient blank field, like the A5's
constexpr int kMireFirstLine = 15;
constexpr int kMirePeriodPx = 31;

// A blank field: lamp on, no specimen.
cv::Mat blankField() {
    return cv::Mat(kFrame, CV_8UC3, kTint);
}

// Dark vertical lines every kMirePeriodPx on a light background, like
// tools/make_mire.py.
cv::Mat mire() {
    cv::Mat image(kFrame, CV_8UC3, cv::Scalar(215, 215, 215));
    for (int x = kMireFirstLine; x < image.cols; x += kMirePeriodPx) {
        cv::line(image, {x, 0}, {x, image.rows - 1}, cv::Scalar(45, 45, 45), 3);
    }
    return image;
}

// A real micrograph: the in-repo legacy crop of a steel, tiled to the frame.
cv::Mat specimen() {
    const fs::path crop = fs::path(MSAPP_SOURCE_DIR) / "tests" / "fixtures" / "legacy" / "CTTM1Ech1Vue1_crop256.png";
    const cv::Mat tile = cv::imread(crop.string(), cv::IMREAD_COLOR);
    if (tile.empty()) throw std::runtime_error("missing fixture " + crop.string());
    cv::Mat image(kFrame, CV_8UC3);
    for (int y = 0; y < image.rows; y += tile.rows) {
        for (int x = 0; x < image.cols; x += tile.cols) {
            const cv::Rect cell(x, y, std::min(tile.cols, image.cols - x), std::min(tile.rows, image.rows - y));
            tile(cv::Rect(0, 0, cell.width, cell.height)).copyTo(image(cell));
        }
    }
    return image;
}

fs::path writeFrame(const fs::path& dir, const std::string& name, const cv::Mat& image) {
    const fs::path path = dir / name;
    if (!cv::imwrite(path.string(), image)) throw std::runtime_error("cannot write " + path.string());
    return path;
}

bool samePixels(const cv::Mat& a, const cv::Mat& b) {
    return a.size() == b.size() && a.type() == b.type() && cv::norm(a, b, cv::NORM_INF) == 0.0;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_e2e_lab_session";
    fs::remove_all(dir);
    fs::create_directories(dir / "frames");
    const fs::path configPath = dir / "config" / "workstation.json";
    const fs::path catalogPath = fs::path(MSAPP_SOURCE_DIR) / "docs" / "catalog-example.json";

    // What the replay camera shows, in order, as the teacher then the
    // student move slides under the objective. It cycles forever.
    const fs::path blankPath = writeFrame(dir / "frames", "blank.png", blankField());
    const fs::path mirePath = writeFrame(dir / "frames", "mire.png", mire());
    const fs::path specimenPath = writeFrame(dir / "frames", "specimen.png", specimen());

    std::cout << "== First launch: no config yet, the catalog from the teacher ==\n";
    WorkstationConfig config;
    {
        const LoadedWorkstationConfig loaded = loadWorkstationConfig(configPath);
        check(loaded.error.empty(), "a missing workstation.json is a first launch, not an error");
        check(loaded.config.calibrations.empty(), "nothing is calibrated yet");
        config = loaded.config;
        config.camera.backend = "file";
        config.camera.frames = {blankPath, mirePath, specimenPath};
        config.camera.width = kFrame.width;
        config.camera.height = kFrame.height;
        config.camera.pixelFormat.clear();
        saveWorkstationConfig(config, configPath);
        check(fs::exists(configPath), "the replay camera profile is saved, its folder created");
    }
    const LoadedCatalog catalog = loadCatalog(catalogPath);
    check(catalog.error.empty(), "the example catalog loads");
    const Tp* tp = catalog.catalog.tp("GMP", "TP 1: Aciers");
    check(tp != nullptr && tp->echantillons.size() == 5, "TP 1: Aciers lists its five Échantillons");
    if (tp == nullptr) return summary();
    const VueLocation ac1{"GMP", tp->name, tp->echantillons[0].name};
    const VueLocation ac2{"GMP", tp->name, tp->echantillons[1].name};

    std::cout << "\n== Teacher: White reference on the blank field (ADR 0014) ==\n";
    WhiteBalance gains;
    {
        CaptureStation station(loadWorkstationConfig(configPath).config.camera);
        const ProfileReport report = station.open();
        check(station.isOpen(), "the replay camera opens");
        check(report.frameSize.empty() || report.frameSize == kFrame, "no mode is invented for image files");
        gains = station.calibrateWhiteReference();
        check(nearlyEqual(gains.red, 120.0 / 180.0, 1e-6) && nearlyEqual(gains.blue, 120.0 / 200.0, 1e-6),
              "gains neutralise the blank field's tint");
        config.camera.whiteReference = gains;
        saveWorkstationConfig(config, configPath);
        station.close();
    }
    {
        const LoadedWorkstationConfig reloaded = loadWorkstationConfig(configPath);
        check(reloaded.error.empty() && nearlyEqual(reloaded.config.camera.whiteReference.red, gains.red) &&
                  nearlyEqual(reloaded.config.camera.whiteReference.blue, gains.blue),
              "the White reference survives a restart");
    }

    std::cout << "\n== Teacher: x10 calibrated on the mire, x40 derived (ADR 0020) ==\n";
    const double x10PxPerMicron = 10.0 * kMirePeriodPx / (10.0 * config.mirePitchMicrons);
    {
        CaptureStation station(loadWorkstationConfig(configPath).config.camera);
        station.open();
        station.grabPreview();  // the blank field is still under the objective
        const cv::Mat still = station.grabPreview();
        check(still.size() == kFrame, "the mire still has the camera's frame size");

        const auto normal = mireLineNormal(still);
        check(normal && std::abs((*normal)[0]) > 0.999, "the mire's lines are found vertical");
        MireCalibration mire("x10", still.cols, still.rows, config.mirePitchMicrons);
        mire.setLineNormal(normal);
        const double lastLine = kMireFirstLine + 10.0 * kMirePeriodPx;
        mire.addReading({kMireFirstLine, 100}, {lastLine, 100}, 10);
        // Second reading: clicks far apart along the lines; only the distance
        // across them counts (#65).
        mire.addReading({kMireFirstLine, 40}, {lastLine, 470}, 10);
        check(mire.agreed(), "two readings agree within 0.25 %");
        const Calibration x10 = mire.result();
        check(nearlyEqual(x10.pixelsPerMicron, x10PxPerMicron, 1e-6), "x10: 310 px across 10 periods of 20 µm");
        check(x10.frameWidth == kFrame.width && x10.frameHeight == kFrame.height,
              "the Calibration records the frame size it was measured at (ADR 0015)");
        config.setCalibration(x10);
        saveWorkstationConfig(config, configPath);
        station.close();
    }
    {
        const WorkstationConfig reloaded = loadWorkstationConfig(configPath).config;
        const auto x40 = reloaded.effectiveCalibration("x40");
        check(x40 && x40->isDerived() && x40->derivedFrom == "x10" &&
                  nearlyEqual(x40->pixelsPerMicron, 4.0 * x10PxPerMicron, 1e-6),
              "after a restart, x40 is derived from x10 by the ratio of magnifications");
        check(!throwsWith<std::runtime_error>([&] { reloaded.calibrationForFrame("x40", 3840, 2160); }).empty(),
              "another camera mode refuses the Calibration instead of rescaling it");
    }

    std::cout << "\n== Student: capture Ac1 at x40 ==\n";
    VueStore store(dir / "vues");
    fs::path vuePath;
    cv::Mat expectedOriginal;
    {
        const WorkstationConfig workstation = loadWorkstationConfig(configPath).config;
        CaptureStation station(workstation.camera);
        station.open();
        station.grabPreview();  // blank field
        station.grabPreview();  // mire
        const auto calibration = workstation.calibrationForFrame("x40", kFrame.width, kFrame.height);
        check(calibration.has_value(), "x40 holds for the frame the camera delivers");
        vuePath = store.newVuePath(ac1);
        const Vue vue = station.capture(vuePath, calibration);
        station.close();
        expectedOriginal = applyWhiteBalanceGains(cv::imread(specimenPath.string(), cv::IMREAD_COLOR), gains);
        check(store.listVues(ac1) == std::vector<fs::path>{vuePath}, "the Vue is filed under its Échantillon");
        check(vue.treatmentCount() == 1 && vue.treatment(0).type() == "echelle",
              "a calibrated capture gets its Échelle straight away (ADR 0017)");
    }

    std::cout << "\n== Next session: the Vue reopens as it was captured ==\n";
    Vue vue = Vue::load(vuePath);
    {
        check(!vue.sidecarError(), "the sidecar reads back without error");
        check(samePixels(vue.original(), expectedOriginal),
              "the stored original is the specimen with the White reference baked in, bit for bit");
        const auto& info = vue.captureInfo();
        check(info && info->cameraBackend == "file" && info->frameWidth == kFrame.width &&
                  info->frameHeight == kFrame.height && nearlyEqual(info->whiteReference.red, gains.red),
              "the sidecar says how it was captured");
        const auto& calibration = vue.calibration();
        check(calibration && calibration->grossissement == "x40" && calibration->derivedFrom == "x10" &&
                  nearlyEqual(calibration->pixelsPerMicron, 4.0 * x10PxPerMicron, 1e-6),
              "the Vue keeps its own copy of the x40 Calibration");
        check(vue.treatmentCount() == 1 && vue.treatment(0).type() == "echelle", "the Échelle reloads");
    }

    std::cout << "\n== Student: measure, Vickers, phase analysis ==\n";
    {
        const double pxPerMicron = vue.calibration()->pixelsPerMicron;
        const Measurement length({100, 100}, {100 + 10.0 * pxPerMicron, 100}, vue);
        check(nearlyEqual(length.realDistanceMicrons(), 10.0, 1e-9), "a length is read in µm with the Vue's Calibration");

        const Measurement d1({200, 200}, {200 + 30.0 * pxPerMicron, 200}, vue);
        const Measurement d2({215, 185}, {215, 185 + 32.0 * pxPerMicron}, vue);
        const VickersResult hv = vickersHardness(d1, d2);
        check(nearlyEqual(hv.hardness, vickersHardness(30.0, 32.0).hardness, 1e-6) &&
                  hv.label().find("HV0.5") != std::string::npos,
              "Vickers on the Vue matches the diagonals in µm, at the legacy 0.5 kgf");

        const PhaseAnalysis phases = analyzeVuePhases(vue.original());
        check(phases.threshold.found, "the steel has a phase threshold");
        check(nearlyEqual(phases.fractions.darkPercent + phases.fractions.lightPercent, 100.0, 1e-3),
              "dark and light phases add up to 100 %");
        const cv::Rect zone(0, 0, 256, 256);
        check(analyzeVuePhases(vue.original(), zone).threshold.found, "a zone of the Vue can be analysed on its own");

        vue.insertTreatment(0, std::make_unique<LevelsTreatment>(10, 240));
        vue.insertTreatment(1, std::make_unique<PhaseOverlayTreatment>(phases.threshold.threshold, zone));
    }
    {
        const Vue reloaded = Vue::load(vuePath);
        check(reloaded.treatmentCount() == 3 && reloaded.treatment(0).type() == "levels" &&
                  reloaded.treatment(1).type() == "phase_overlay" && reloaded.treatment(2).type() == "echelle",
              "Levels, phase map and Échelle reload in the order applied");
        check(samePixels(reloaded.original(), expectedOriginal), "Treatments never touch the stored original");
        const cv::Mat rendered = reloaded.render();
        check(rendered.size() == kFrame && !samePixels(rendered, expectedOriginal), "render() applies them");
        const fs::path exported = dir / "export" / "ac1.png";
        fs::create_directories(exported.parent_path());
        reloaded.exportRendered(exported);
        check(samePixels(cv::imread(exported.string(), cv::IMREAD_COLOR), rendered),
              "the exported copy is exactly what render() shows");
    }

    std::cout << "\n== Student: flag the Vue principale for the TP's Fiche (ADR 0016, 0019) ==\n";
    {
        store.setPrincipale(ac1, vuePath);
        const std::vector<FicheEntry> entries = ficheEntries(store, "GMP", *tp);
        check(entries.size() == 5 && entries[0].vue == vuePath, "Ac1 shows its Vue principale on the Fiche");
        check(!entries[1].vue && !entries[4].vue, "the other Échantillons have an empty frame");
        const std::vector<FicheCell> cells =
            layoutFiche(entries.size(), static_cast<double>(kFrame.width) / kFrame.height);
        check(cells.size() == 5 && cells.back().page == 1, "five Échantillons, three per page: two pages");
    }

    std::cout << "\n== Student: file a stray Unsorted Vue, delete one (ADR 0008, ADR 0004) ==\n";
    {
        CaptureStation station(loadWorkstationConfig(configPath).config.camera);
        station.open();
        const fs::path stray = store.newVuePath(VueLocation::unsorted());
        station.capture(stray);  // no Échantillon picked: uncalibrated, into Unsorted
        station.close();
        check(store.listVues(VueLocation::unsorted()).size() == 1, "an uncalibrated capture lands in Unsorted");

        const fs::path filed = store.fileVue(stray, ac2);
        check(store.listVues(VueLocation::unsorted()).empty() && store.listVues(ac2) == std::vector<fs::path>{filed},
              "Classer la Vue moves it under its Échantillon");
        check(fs::exists(fs::path(filed.string() + ".json")), "its sidecar moves with it");

        const fs::path binned = store.deleteVue(vuePath);
        check(fs::exists(binned) && !fs::exists(vuePath), "a deleted Vue goes to the Corbeille, not away");
        check(store.listVues(ac1).empty() && !store.principale(ac1), "it is no longer listed nor flagged");
        check(!ficheEntries(store, "GMP", *tp)[0].vue, "the Fiche shows Ac1's frame empty again");
    }

    fs::remove_all(dir);
    return summary();
}
