#pragma once

#include <string>

namespace msapp {

// Formats a microns value without trailing zeros (e.g. "50 um", "0.5 um",
// "200 um"). Plain ASCII, not "µm" — Hershey fonts (used to bake Échelle's
// scale-bar label directly into the saved image) can't render "µ"
// reliably, and Measurement's on-screen label uses the same convention so
// the two never visually disagree.
std::string formatMicrons(double microns);

} // namespace msapp
