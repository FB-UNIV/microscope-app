// User journey:
//   As a teacher at the end of a TP, I print its Fiche: every Échantillon
//   with its Vue principale, three per A4 page, one under the other, large
//   enough to read the grains — eight Échantillons print on three pages —
//   and an Échantillon nobody flagged a Vue principale for shows up as such
//   instead of being skipped (ADR 0016, ADR 0019). Each photo prints as
//   captured, with its Échelle but without the Levels or phase map set on
//   screen (ADR 0022).

#include "echelle_treatment.hpp"
#include "fiche.hpp"
#include "levels_treatment.hpp"
#include "phase_overlay_treatment.hpp"
#include "vue.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

bool insidePage(const FicheCell& cell, const FichePage& page) {
    const double eps = 1e-9;
    return cell.x >= page.margin - eps && cell.y >= page.margin - eps &&
           cell.x + cell.width <= page.width - page.margin + eps &&
           cell.y + cell.imageHeight + page.captionHeight <= page.height - page.margin + eps;
}

bool samePixels(const cv::Mat& a, const cv::Mat& b) {
    return a.size() == b.size() && a.type() == b.type() && cv::norm(a, b, cv::NORM_INF) == 0.0;
}

} // namespace

int main() {
    const FichePage a4;

    std::cout << "== layoutFiche: three per A4 page, one per row ==\n";
    {
        const auto cells = layoutFiche(8, 16.0 / 9.0);
        check(cells.size() == 8 && cells[2].page == 0 && cells[3].page == 1 && cells[5].page == 1 &&
                  cells[6].page == 2 && cells.back().page == 2,
              "eight 16:9 Échantillons print on three pages: 3 + 3 + 2");
        // 273 mm of usable height = 3 rows of (75 mm photo + 12 mm caption)
        // with two 6 mm gaps; 75 mm at 16:9 is 133.3 mm wide.
        check(nearlyEqual(cells[0].imageHeight, 75.0) && nearlyEqual(cells[0].width, 75.0 * 16.0 / 9.0),
              "each photo is a third of the page high, at its aspect ratio");
        check(nearlyEqual(cells[0].y, 12.0) && nearlyEqual(cells[1].y, 105.0) && nearlyEqual(cells[2].y, 198.0) &&
                  nearlyEqual(cells[3].y, 12.0),
              "one under the other, and the fourth starts the next page at the top");
        check(nearlyEqual(cells[0].x + cells[0].width / 2, a4.width / 2) && nearlyEqual(cells[1].x, cells[0].x),
              "each photo is centred across the page");
        bool allInside = true;
        for (const auto& cell : cells) allInside = allInside && insidePage(cell, a4);
        check(allInside, "every photo and caption stays inside the margins");

        const auto fourThree = layoutFiche(4, 4.0 / 3.0);
        check(nearlyEqual(fourThree[0].imageHeight, 75.0) && nearlyEqual(fourThree[0].width, 100.0) &&
                  fourThree[2].page == 0 && fourThree[3].page == 1,
              "4:3 photos get the same height, narrower, still three per page");

        const auto wide = layoutFiche(1, 4.0);
        check(nearlyEqual(wide[0].width, 186.0) && nearlyEqual(wide[0].x, 12.0) && insidePage(wide[0], a4),
              "a panorama too wide for its row is limited by the page width");

        const auto tall = layoutFiche(3, 0.2);
        check(tall[2].page == 0 && nearlyEqual(tall[0].imageHeight, 75.0) && insidePage(tall[0], a4),
              "a tall narrow photo keeps its row, three per page");

        FichePage grid;
        grid.columns = 2;
        grid.rows = 4;
        const auto twoByFour = layoutFiche(9, 16.0 / 9.0, grid);
        check(nearlyEqual(twoByFour[0].x, 12.0) && nearlyEqual(twoByFour[1].x, 108.0) &&
                  nearlyEqual(twoByFour[1].y, 12.0) && twoByFour[7].page == 0 && twoByFour[8].page == 1,
              "a page set to two columns lays out left to right, then down");

        check(layoutFiche(0, 16.0 / 9.0).empty(), "a TP with no Échantillon lays out nothing");
        check(!throwsWith<std::invalid_argument>([] { layoutFiche(1, 0.0); }).empty(),
              "an aspect ratio that isn't positive is refused");
        FichePage cramped;
        cramped.captionHeight = 100.0;
        check(!throwsWith<std::invalid_argument>([&] { layoutFiche(1, 16.0 / 9.0, cramped); }).empty(),
              "a page with no room left for a photo is refused");
    }

    std::cout << "\n== ficheEntries: each Échantillon with its Vue principale ==\n";
    {
        const fs::path dir = fs::temp_directory_path() / "msapp_fiche_test";
        fs::remove_all(dir);
        const VueStore store(dir);
        const Tp tp{"TP 1: Aciers", "", {{"Ac1: XC10"}, {"Ac2: XC38"}, {"Ac3: XC80"}}};
        const cv::Mat frame(9, 16, CV_8UC3, cv::Scalar(50, 100, 150));

        const Vue first = Vue::capture(frame, store.newVuePath({"GMP", tp.name, "Ac1: XC10"}));
        Vue::capture(frame, store.newVuePath({"GMP", tp.name, "Ac1: XC10"}));
        Vue::capture(frame, store.newVuePath({"GMP", tp.name, "Ac2: XC38"}));
        store.setPrincipale({"GMP", tp.name, "Ac1: XC10"}, first.imagePath());

        const auto entries = ficheEntries(store, "GMP", tp);
        check(entries.size() == 3 && entries[0].echantillon == "Ac1: XC10" && entries[2].echantillon == "Ac3: XC80",
              "every Échantillon of the TP, in catalog order");
        check(entries[0].vue == first.imagePath(), "with its flagged Vue principale");
        check(!entries[1].vue, "an Échantillon with Vues but none flagged has no Vue principale on the Fiche");
        check(!entries[2].vue, "and neither has one with no Vue at all");
        fs::remove_all(dir);
    }

    std::cout << "\n== fichePhoto: the original with only its Échelle (ADR 0022) ==\n";
    {
        const fs::path dir = fs::temp_directory_path() / "msapp_fiche_photo_test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        // A dark phase on the left, a light one on the right: Levels and the
        // phase map both change every pixel of it.
        cv::Mat frame(180, 320, CV_8UC3, cv::Scalar::all(60));
        frame(cv::Rect(160, 0, 160, 180)).setTo(cv::Scalar::all(180));
        const Calibration x10{"x10", 2.0, 320, 180};
        const cv::Mat withEchelle = EchelleTreatment(x10).apply(frame);

        // As captured (the Échelle first, ADR 0017), then treated on screen.
        {
            Vue vue = Vue::capture(frame, dir / "treated.png", std::nullopt, x10);
            vue.addTreatment(std::make_unique<EchelleTreatment>(x10));
            vue.addTreatment(std::make_unique<LevelsTreatment>(40, 200));
            vue.addTreatment(std::make_unique<PhaseOverlayTreatment>(120));
        }
        const Vue treated = Vue::load(dir / "treated.png");
        const FichePhoto photo = fichePhoto(treated);
        check(samePixels(photo.image, withEchelle) && photo.echelle,
              "a treated Vue prints as its original with the Échelle drawn on it");
        check(photo.image.at<cv::Vec3b>(10, 10) == cv::Vec3b(60, 60, 60) &&
                  photo.image.at<cv::Vec3b>(10, 300) == cv::Vec3b(180, 180, 180),
              "neither the Levels nor the phase map are applied: both phases keep their grey");
        check(!samePixels(treated.render(), withEchelle) && treated.treatmentCount() == 3 &&
                  samePixels(treated.original(), frame),
              "on screen the Vue keeps all three Treatments, and its original is untouched");

        // Levels put before the Échelle so the bar keeps its contrast (ADR 0002).
        {
            Vue vue = Vue::capture(frame, dir / "levels_first.png", std::nullopt, x10);
            vue.addTreatment(std::make_unique<LevelsTreatment>(40, 200));
            vue.addTreatment(std::make_unique<EchelleTreatment>(x10));
            check(samePixels(fichePhoto(vue).image, withEchelle),
                  "the order on screen doesn't matter: still the bare original under the bar");
        }

        {
            Vue vue = Vue::capture(frame, dir / "echelle_removed.png", std::nullopt, x10);
            vue.addTreatment(std::make_unique<EchelleTreatment>(x10));
            vue.addTreatment(std::make_unique<LevelsTreatment>(40, 200));
            vue.removeTreatment(0);
            const FichePhoto bare = fichePhoto(vue);
            check(samePixels(bare.image, frame) && !bare.echelle && vue.calibration(),
                  "a calibrated Vue whose Échelle was removed prints bare: no bar drawn from its Calibration");
        }

        {
            const Vue plain = Vue::capture(frame, dir / "plain.png");
            const FichePhoto bare = fichePhoto(plain);
            check(samePixels(bare.image, frame) && !bare.echelle, "a Vue with no Treatment prints its original");
        }
        {
            Vue uncalibrated = Vue::capture(frame, dir / "uncalibrated.png");
            uncalibrated.addTreatment(std::make_unique<LevelsTreatment>(40, 200));
            const FichePhoto bare = fichePhoto(uncalibrated);
            check(samePixels(bare.image, frame) && !bare.echelle,
                  "an uncalibrated Vue with Levels prints its original, without Échelle");
        }
        fs::remove_all(dir);
    }

    return summary();
}
