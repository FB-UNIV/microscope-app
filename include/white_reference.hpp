#pragma once

#include "white_balance.hpp"

#include <opencv2/core.hpp>

namespace msapp {

// White reference (see CONTEXT.md, ADR 0014): R/G/B gains measured once per
// microscope from a blank field — lamp on, no specimen or a mirror-polished
// area — and baked into every capture. It corrects the green/magenta tint a
// UVC camera's colour-temperature control cannot reach (the Moticam A5 is
// green-deficient), on top of a locked colour temperature that stops
// auto-white-balance drift between frames.

// Gains that make the blank field neutral, normalised to green = 1. Uses
// the central half of the frame (avoids vignetting) and ignores pixels with
// any channel >= 250 (clipped pixels carry no colour information). Throws
// std::invalid_argument if the input isn't 8-bit BGR, if the usable region
// is too dark (mean green < 20) or mostly clipped, or if a gain falls
// outside [0.25, 4] — a sign the frame wasn't a blank field.
WhiteBalance computeWhiteReferenceGains(const cv::Mat& blankFieldBgr);

// Applies gains with saturation. Grey (single-channel) input is returned
// unchanged — gains only make sense on colour.
cv::Mat applyWhiteBalanceGains(const cv::Mat& image, const WhiteBalance& gains);

// The live preview's version, for display only: `image` is first shrunk
// (INTER_AREA, aspect ratio kept) to fit inside `fitWithin` when larger,
// then the gains are applied through a lookup table — on a 4K frame shown
// at 1920x1080, a quarter of the pixels. Against shrinking
// applyWhiteBalanceGains()'s result it differs by rounding only (a level or
// two: the gain times half a level), except next to pixels a gain clips:
// clipping after the average, the preview is brighter there. An empty
// `fitWithin` keeps the size. With identity gains or grey input, nothing is
// applied and the result may share `image`'s pixels. Throws
// std::invalid_argument for anything but 8-bit grey, BGR or BGRA.
cv::Mat whiteBalancedPreview(const cv::Mat& image, const WhiteBalance& gains, cv::Size fitWithin);

} // namespace msapp
