#pragma once

#include "calibration.hpp"
#include <opencv2/core.hpp>
#include <string>

namespace msapp {

class Vue;

// Measurement (see CONTEXT.md): an interactive, ephemeral point-to-point
// tool. Given two points on a Vue's rendered image and that Vue's
// Calibration, reports the real-world distance between them. Unlike
// Échelle, a Measurement never mutates the photo — it's a read-only
// calculation, ephemeral by design (confirmed in conversation: it's
// discarded once the operator moves on, never committed as an
// annotation).
class Measurement {
public:
    Measurement(cv::Point2d p1, cv::Point2d p2, Calibration calibration);
    // Points on `vue`, measured with the Calibration it was captured or
    // imported with — already checked against its frame size (ADR 0015).
    // Throws std::invalid_argument when the Vue has no Calibration.
    Measurement(cv::Point2d p1, cv::Point2d p2, const Vue& vue);

    double pixelDistance() const;
    double realDistanceMicrons() const;

    // Human-readable label, e.g. "2 um" or "2.5 um" — same plain-ASCII
    // formatting convention as Échelle's baked-in overlay label (Hershey
    // fonts can't render "µ" reliably; see echelle_treatment.cpp).
    std::string label() const;

private:
    cv::Point2d p1_;
    cv::Point2d p2_;
    Calibration calibration_;
};

} // namespace msapp
