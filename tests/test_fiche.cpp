// User journey:
//   As a teacher at the end of a TP, I print its Fiche: every Échantillon
//   with its Vue principale, on as few A4 pages as fit — eight Échantillons
//   photographed in 16:9 fit on one page — and an Échantillon nobody flagged
//   a Vue principale for shows up as such instead of being skipped
//   (ADR 0016).

#include "fiche.hpp"
#include "vue.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <filesystem>
#include <iostream>
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

} // namespace

int main() {
    const FichePage a4;

    std::cout << "== layoutFiche: as few A4 pages as fit ==\n";
    {
        const auto cells = layoutFiche(8, 16.0 / 9.0);
        check(cells.size() == 8 && cells.back().page == 0, "eight 16:9 Échantillons fit on one A4 page");
        check(nearlyEqual(cells[0].x, 12.0) && nearlyEqual(cells[0].y, 12.0) && nearlyEqual(cells[1].x, 108.0) &&
                  nearlyEqual(cells[1].y, 12.0) && nearlyEqual(cells[2].x, 12.0),
              "two per row, left to right then down");
        check(nearlyEqual(cells[0].width, 90.0) && nearlyEqual(cells[0].imageHeight, 90.0 * 9.0 / 16.0),
              "each photo is a half-page wide, at its aspect ratio");
        bool allInside = true;
        for (const auto& cell : cells) allInside = allInside && insidePage(cell, a4);
        check(allInside, "every photo and caption stays inside the margins");

        const auto nine = layoutFiche(9, 16.0 / 9.0);
        check(nine[8].page == 1 && nearlyEqual(nine[8].x, 12.0) && nearlyEqual(nine[8].y, 12.0),
              "a ninth Échantillon starts a second page, top left");

        const auto fourThree = layoutFiche(7, 4.0 / 3.0);
        check(fourThree[5].page == 0 && fourThree[6].page == 1, "4:3 photos are taller: six per page");

        const auto tall = layoutFiche(3, 0.2);
        check(tall[0].imageHeight + a4.captionHeight <= a4.height - 2 * a4.margin + 1e-9 && insidePage(tall[0], a4) &&
                  tall[2].page == 1,
              "a photo too tall for a row is shrunk to fit a page");

        check(layoutFiche(0, 16.0 / 9.0).empty(), "a TP with no Échantillon lays out nothing");
        check(!throwsWith<std::invalid_argument>([] { layoutFiche(1, 0.0); }).empty(),
              "an aspect ratio that isn't positive is refused");
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

    return summary();
}
