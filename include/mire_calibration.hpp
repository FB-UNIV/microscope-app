#pragma once

#include "calibration.hpp"

#include <opencv2/core.hpp>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// The lab's mire (reticle): one line every 20 µm (CONTEXT.md, Calibration).
constexpr double kMirePitchMicrons = 20.0;
// The legacy procedure repeats until two readings agree within 0.25 %
// (docs/legacy/reverse-engineering-report.md, §3.3b).
constexpr double kMireAgreement = 0.0025;

// Calibrating one Grossissement against the mire (issue #22): the operator
// clicks a mire line and the line `periods` lines further along, as many
// periods apart as the frame allows (more periods, less click error); each
// reading gives px/µm. Readings repeat until the newest two agree within
// 0.25 %; their mean is the Calibration, recorded with the frame size it
// was measured at (ADR 0015).
class MireCalibration {
public:
    // `frameWidth` x `frameHeight` is the size of the frames clicked on:
    // the camera's actual mode, not the profile's request. Throws
    // std::invalid_argument for an empty Grossissement, a frame size that
    // isn't positive, or a pitch that isn't a positive number.
    MireCalibration(std::string grossissement, int frameWidth, int frameHeight,
                    double pitchMicrons = kMirePitchMicrons);

    // The direction across the mire's lines (mireLineNormal()), so a reading
    // counts only the distance between the two lines clicked, not how far
    // apart along them the two clicks are (#65). Empty: the straight
    // distance between the clicks, which assumes they are level across the
    // lines. Applies to readings added from now on. Throws
    // std::invalid_argument for a zero vector.
    void setLineNormal(std::optional<cv::Vec2d> normal);

    // Records the reading from two clicked points `periods` mire periods
    // apart and returns its px/µm. Throws std::invalid_argument, recording
    // nothing, when `periods` < 1 or the points are less than one pixel
    // apart across the lines.
    double addReading(cv::Point2d first, cv::Point2d last, int periods);

    // The distance between two clicks that addReading() counts.
    double measuredPixels(cv::Point2d first, cv::Point2d last) const;

    // Every reading so far, oldest first.
    const std::vector<double>& readings() const { return readings_; }

    // |newest - previous| / their mean; empty before the second reading.
    std::optional<double> lastDifference() const;

    // True once the newest two readings agree within 0.25 %.
    bool agreed() const;

    // The Calibration from the two agreeing readings: their mean px/µm, this
    // Grossissement, this frame size. Throws std::logic_error before
    // agreed().
    Calibration result() const;

    // Forgets every reading, e.g. after the mire was moved.
    void reset() { readings_.clear(); }

private:
    std::string grossissement_;
    int frameWidth_;
    int frameHeight_;
    double pitchMicrons_;
    std::optional<cv::Vec2d> normal_;  // unit length
    std::vector<double> readings_;
};

// The direction across the parallel lines of a mire image, as a unit
// vector: (1, 0) for vertical lines. Found from the image's gradients
// (structure tensor), so a mire set a little askew under the objective is
// measured right. Empty when no single direction stands out — a blank or
// noisy image, or not a mire — and the clicks are then measured as they
// are. Accepts 8-bit grey, BGR or BGRA.
std::optional<cv::Vec2d> mireLineNormal(const cv::Mat& image);

} // namespace msapp
