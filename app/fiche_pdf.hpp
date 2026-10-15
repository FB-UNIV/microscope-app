#pragma once

#include "catalog.hpp"
#include "vue_store.hpp"

#include <QString>
#include <QStringList>

#include <string>

namespace msapp {

// Writes the Fiche of `tp` to `pdfPath` (issue #27, ADR 0016), laid out by
// layoutFiche(): each Échantillon's Vue principale rendered with its
// Treatments — so with its Échelle when calibrated (ADR 0017) — its name
// and its Grossissement under it, nothing else. Photos are shrunk to print
// resolution (300 dpi) so the file stays small. An Échantillon with no Vue
// principale, or one that can't be read, gets an empty frame saying so; an
// uncalibrated one prints with "Grossissement inconnu". Returns one line
// per such Échantillon, for the window to report. Throws std::runtime_error
// when the PDF can't be written.
QStringList writeFichePdf(const VueStore& store, const std::string& department, const Tp& tp, const QString& pdfPath);

} // namespace msapp
