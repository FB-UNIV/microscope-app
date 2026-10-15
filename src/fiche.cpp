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
    const double usableWidth = page.width - 2 * page.margin;
    const double usableHeight = page.height - 2 * page.margin;
    double cellWidth = (usableWidth - (columns - 1) * page.gap) / columns;
    double imageHeight = cellWidth / imageAspect;
    // A photo taller than a page row: shrink it to fill one page instead.
    if (imageHeight + page.captionHeight > usableHeight) {
        imageHeight = usableHeight - page.captionHeight;
        cellWidth = imageHeight * imageAspect;
    }
    if (cellWidth <= 0.0 || imageHeight <= 0.0) {
        throw std::invalid_argument("layoutFiche: the page has no room for a single photo");
    }
    const double rowHeight = imageHeight + page.captionHeight;
    const int rowsPerPage = std::max(1, static_cast<int>(std::floor((usableHeight + page.gap) / (rowHeight + page.gap))));
    const int perPage = rowsPerPage * columns;

    std::vector<FicheCell> cells;
    cells.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int index = static_cast<int>(i);
        const int onPage = index % perPage;
        FicheCell cell;
        cell.page = index / perPage;
        cell.x = page.margin + (onPage % columns) * (cellWidth + page.gap);
        cell.y = page.margin + (onPage / columns) * (rowHeight + page.gap);
        cell.width = cellWidth;
        cell.imageHeight = imageHeight;
        cells.push_back(cell);
    }
    return cells;
}

} // namespace msapp
