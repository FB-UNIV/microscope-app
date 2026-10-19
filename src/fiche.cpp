#include "fiche.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace msapp {

std::vector<FicheEntry> ficheEntries(const VueStore& store, const std::string& department, const Tp& tp) {
    std::vector<FicheEntry> entries;
    entries.reserve(tp.echantillons.size());
    for (const auto& echantillon : tp.echantillons) {
        entries.push_back({echantillon.name, store.principale({department, tp.name, echantillon.name})});
    }
    return entries;
}

std::vector<FicheCell> layoutFiche(std::size_t count, double imageAspect, const FichePage& page) {
    if (!std::isfinite(imageAspect) || imageAspect <= 0.0) {
        throw std::invalid_argument("layoutFiche: the photos' aspect ratio must be a positive number");
    }
    const int columns = std::max(1, page.columns);
    const int rows = std::max(1, page.rows);
    const double usableWidth = page.width - 2 * page.margin;
    const double usableHeight = page.height - 2 * page.margin;
    // The largest photo that fits both its column and its row with the
    // caption under it.
    const double widthLimit = (usableWidth - (columns - 1) * page.gap) / columns;
    const double heightLimit = (usableHeight - (rows - 1) * page.gap) / rows - page.captionHeight;
    const double imageHeight = std::min(widthLimit / imageAspect, heightLimit);
    const double cellWidth = imageHeight * imageAspect;
    if (cellWidth <= 0.0 || imageHeight <= 0.0) {
        throw std::invalid_argument("layoutFiche: the page has no room for a single photo");
    }
    const double rowHeight = imageHeight + page.captionHeight;
    const double rowWidth = columns * cellWidth + (columns - 1) * page.gap;
    const double left = page.margin + (usableWidth - rowWidth) / 2;
    const int perPage = rows * columns;

    std::vector<FicheCell> cells;
    cells.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int index = static_cast<int>(i);
        const int onPage = index % perPage;
        FicheCell cell;
        cell.page = index / perPage;
        cell.x = left + (onPage % columns) * (cellWidth + page.gap);
        cell.y = page.margin + (onPage / columns) * (rowHeight + page.gap);
        cell.width = cellWidth;
        cell.imageHeight = imageHeight;
        cells.push_back(cell);
    }
    return cells;
}

} // namespace msapp
