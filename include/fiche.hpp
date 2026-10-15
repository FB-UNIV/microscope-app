#pragma once

#include "catalog.hpp"
#include "vue_store.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// Fiche (CONTEXT.md, ADR 0016): one sheet per TP showing, for each of its
// Échantillons, the name, the Vue principale rendered with its Treatments,
// its Grossissement and its Échelle — nothing else — on as few A4 pages as
// fit. Generated as a file first, printed from there (ADR 0010). This is
// what goes on it and where; drawing the PDF is the app's job (Qt).

struct FicheEntry {
    std::string echantillon;
    // Its Vue principale; empty when none is flagged — the sheet shows an
    // empty frame saying so rather than guessing which Vue was meant.
    std::optional<std::filesystem::path> vue;
};

// The Échantillons of `tp` (in `department`), in catalog order, each with
// its Vue principale as VueStore records it.
std::vector<FicheEntry> ficheEntries(const VueStore& store, const std::string& department, const Tp& tp);

// A page and its spacing, in millimetres: A4 portrait by default.
struct FichePage {
    double width = 210.0;
    double height = 297.0;
    double margin = 12.0;
    double gap = 6.0;            // between cells, both ways
    double captionHeight = 12.0; // name and Grossissement, under the photo
    int columns = 2;
};

// Where one Échantillon goes: its photo in the box (x, y, width,
// imageHeight) — fitted inside, keeping its own aspect ratio — and its
// caption in the captionHeight under it.
struct FicheCell {
    int page = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double imageHeight = 0.0;
};

// Lays out `count` cells row by row, `columns` per row, as many rows per page
// as fit, for photos `imageAspect` wide per unit of height (16:9 for the A5's
// 4K mode). A photo too tall for one row is shrunk to fit a page. Throws
// std::invalid_argument for an aspect that isn't a positive number, or a
// page with no room for one cell.
std::vector<FicheCell> layoutFiche(std::size_t count, double imageAspect, const FichePage& page = FichePage{});

} // namespace msapp
