#pragma once

#include "measurement.hpp"
#include <string>

namespace msapp {

// Vickers hardness (see CONTEXT.md, legacy "Mesure de dureté"): the operator
// measures the two diagonals of a pyramid indent; HV follows from their mean.
//
// Coefficient recovered from the legacy app (0x44aad0):
//   HV = 0.927045 / d_um^2 * 1e6 at the fixed 0.5 kgf load
//      = 1.85409 * F_kgf / d_mm^2.
// ISO 6507 states 1.8544; the 0.015 % difference is kept on purpose so new
// readings line up with archived ones (ADR 0012). The legacy app hard-coded
// F = 0.5 kgf (HV0.5); the load is a parameter here, defaulting to that.
constexpr double kLegacyVickersCoefficient = 1.85409;
constexpr double kLegacyVickersLoadKgf = 0.5;

struct VickersResult {
    double diagonal1Microns = 0.0;
    double diagonal2Microns = 0.0;
    double meanDiagonalMicrons = 0.0;
    double loadKgf = kLegacyVickersLoadKgf;
    double hardness = 0.0;

    // ISO 6507 notation, e.g. "370.8 HV0.5".
    std::string label() const;
};

VickersResult vickersHardness(double diagonal1Microns, double diagonal2Microns,
                              double loadKgf = kLegacyVickersLoadKgf);

// Convenience for the UI: the two diagonals as Measurements on the Vue,
// each already carrying its Calibration.
VickersResult vickersHardness(const Measurement& diagonal1, const Measurement& diagonal2,
                              double loadKgf = kLegacyVickersLoadKgf);

} // namespace msapp
