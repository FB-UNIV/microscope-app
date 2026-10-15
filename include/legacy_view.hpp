#pragma once

#include "calibration.hpp"
#include "vue.hpp"

#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <string>

namespace msapp {

// Legacy view files: TP<t>Ech<e>Vue<v>.tif (or CTTM<t>Ech<e>Vue<v>.tif),
// 8-bit RGB TIFFs written by the legacy app with a private tag 0xF000 that
// carries the objective and the sample-plane calibration. Importing keeps
// that calibration instead of asking someone to re-measure old photos.

struct LegacyViewName {
    std::string prefix;  // "TP" or "CTTM"
    int tp = 0;
    int echantillon = 0;
    int vue = 0;
};

std::optional<LegacyViewName> parseLegacyViewFileName(const std::string& fileName);

struct LegacyViewMetadata {
    double grossissement = 0.0;  // field 0x200: objective, e.g. 50
    double pixelsPerMm = 0.0;    // field 0x207: sample-plane px/mm (legacy 288.65 x objective)
    std::map<int, double> fields;  // every numeric field, incl. ones not understood yet

    // e.g. {"x50", 14.4325}. The tag holds no frame size, so it is left
    // unknown here; importLegacyView fills it in from the image (ADR 0015).
    Calibration calibration() const;
};

// nullopt when the TIFF has no tag 0xF000 or it lacks fields 0x200/0x207.
// Throws std::runtime_error on a stream that isn't a TIFF.
std::optional<LegacyViewMetadata> parseLegacyViewMetadata(std::istream& tiff);
std::optional<LegacyViewMetadata> readLegacyViewMetadata(const std::filesystem::path& tiff);

struct LegacyViewImport {
    Vue vue;  // pixels copied into a new immutable Vue (ADR 0003/0004)
    std::optional<LegacyViewName> name;
    std::optional<LegacyViewMetadata> metadata;
};

// Copies a legacy view into a new Vue at newImagePath (which must not exist)
// and returns what the legacy file said about it. Adds no Treatments. The
// legacy calibration, sized to the view's own pixels, is stored in the new
// Vue's sidecar, so it survives a reload (ADR 0015).
LegacyViewImport importLegacyView(const std::filesystem::path& legacyTiff,
                                  const std::filesystem::path& newImagePath);

} // namespace msapp
