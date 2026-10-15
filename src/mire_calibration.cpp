#include "mire_calibration.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace msapp {

MireCalibration::MireCalibration(std::string grossissement, int frameWidth, int frameHeight, double pitchMicrons)
    : grossissement_(std::move(grossissement)),
      frameWidth_(frameWidth),
      frameHeight_(frameHeight),
      pitchMicrons_(pitchMicrons) {
    if (grossissement_.empty()) {
        throw std::invalid_argument("MireCalibration: no Grossissement given");
    }
    // ADR 0015: a Calibration must record the frame size it was measured at.
    if (frameWidth_ <= 0 || frameHeight_ <= 0) {
        throw std::invalid_argument("MireCalibration: frame size " + std::to_string(frameWidth_) + "x" +
                                    std::to_string(frameHeight_) + " is not a real frame size");
    }
    if (!std::isfinite(pitchMicrons_) || pitchMicrons_ <= 0.0) {
        throw std::invalid_argument("MireCalibration: the mire pitch must be a positive number of µm");
    }
}

double MireCalibration::addReading(cv::Point2d first, cv::Point2d last, int periods) {
    if (periods < 1) {
        throw std::invalid_argument("MireCalibration::addReading: the points must be at least one mire period apart, "
                                    "got " + std::to_string(periods));
    }
    const double pixels = std::hypot(last.x - first.x, last.y - first.y);
    if (!(pixels >= 1.0)) {  // also catches NaN
        throw std::invalid_argument("MireCalibration::addReading: the two points are less than one pixel apart");
    }
    const double pixelsPerMicron = pixels / (periods * pitchMicrons_);
    readings_.push_back(pixelsPerMicron);
    return pixelsPerMicron;
}

std::optional<double> MireCalibration::lastDifference() const {
    if (readings_.size() < 2) return std::nullopt;
    const double newest = readings_[readings_.size() - 1];
    const double previous = readings_[readings_.size() - 2];
    return std::abs(newest - previous) / ((newest + previous) / 2.0);
}

bool MireCalibration::agreed() const {
    const auto difference = lastDifference();
    return difference && *difference <= kMireAgreement;
}

Calibration MireCalibration::result() const {
    if (!agreed()) {
        throw std::logic_error("MireCalibration::result: the last two readings don't agree within 0.25 % yet");
    }
    const double mean = (readings_[readings_.size() - 1] + readings_[readings_.size() - 2]) / 2.0;
    return Calibration{grossissement_, mean, frameWidth_, frameHeight_};
}

} // namespace msapp
