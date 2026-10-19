#include "mire_calibration.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace msapp {

namespace {

// How strongly one direction must dominate the image's gradients for it to
// be taken as the mire's (coherence, 0..1). Parallel lines give close to 1.
constexpr double kMireMinCoherence = 0.6;

} // namespace

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

void MireCalibration::setLineNormal(std::optional<cv::Vec2d> normal) {
    if (normal) {
        const double length = cv::norm(*normal);
        if (!(length > 0.0)) {
            throw std::invalid_argument("MireCalibration::setLineNormal: a zero vector has no direction");
        }
        *normal /= length;
    }
    normal_ = normal;
}

double MireCalibration::measuredPixels(cv::Point2d first, cv::Point2d last) const {
    const cv::Point2d step = last - first;
    if (normal_) return std::abs(step.x * (*normal_)[0] + step.y * (*normal_)[1]);
    return std::hypot(step.x, step.y);
}

double MireCalibration::addReading(cv::Point2d first, cv::Point2d last, int periods) {
    if (periods < 1) {
        throw std::invalid_argument("MireCalibration::addReading: the points must be at least one mire period apart, "
                                    "got " + std::to_string(periods));
    }
    const double pixels = measuredPixels(first, last);
    if (!(pixels >= 1.0)) {  // also catches NaN
        throw std::invalid_argument("MireCalibration::addReading: the two points are less than one pixel apart "
                                    "across the mire's lines");
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

std::optional<cv::Vec2d> mireLineNormal(const cv::Mat& image) {
    cv::Mat grey;
    if (image.type() == CV_8UC3) {
        cv::cvtColor(image, grey, cv::COLOR_BGR2GRAY);
    } else if (image.type() == CV_8UC4) {
        cv::cvtColor(image, grey, cv::COLOR_BGRA2GRAY);
    } else if (image.type() == CV_8UC1) {
        grey = image;
    } else {
        throw std::invalid_argument("mireLineNormal: expected an 8-bit grey, BGR or BGRA image");
    }
    if (grey.rows < 3 || grey.cols < 3) return std::nullopt;
    cv::Mat gx, gy;
    cv::Sobel(grey, gx, CV_32F, 1, 0);
    cv::Sobel(grey, gy, CV_32F, 0, 1);
    // Structure tensor summed over the whole image: its main eigenvector is
    // the direction the grey level changes most, i.e. across the lines.
    const double jxx = cv::sum(gx.mul(gx))[0];
    const double jyy = cv::sum(gy.mul(gy))[0];
    const double jxy = cv::sum(gx.mul(gy))[0];
    const double total = jxx + jyy;
    if (!(total > 0.0)) return std::nullopt;
    // 1 for perfectly parallel lines, 0 for no preferred direction.
    const double coherence = std::sqrt((jxx - jyy) * (jxx - jyy) + 4.0 * jxy * jxy) / total;
    if (coherence < kMireMinCoherence) return std::nullopt;
    const double angle = 0.5 * std::atan2(2.0 * jxy, jxx - jyy);
    cv::Vec2d normal(std::cos(angle), std::sin(angle));
    // One sign for a given mire, pointing right (or down for level lines).
    if (normal[0] < 0.0 || (normal[0] == 0.0 && normal[1] < 0.0)) normal = -normal;
    return normal;
}

} // namespace msapp
