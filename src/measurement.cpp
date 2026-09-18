#include "measurement.hpp"
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace msapp {

Measurement::Measurement(cv::Point2d p1, cv::Point2d p2, Calibration calibration)
    : p1_(p1), p2_(p2), calibration_(std::move(calibration)) {
    if (calibration_.pixelsPerMicron <= 0.0) {
        throw std::invalid_argument("Measurement: Calibration.pixelsPerMicron must be > 0, got " +
                                     std::to_string(calibration_.pixelsPerMicron));
    }
}

double Measurement::pixelDistance() const {
    return cv::norm(p1_ - p2_);
}

double Measurement::realDistanceMicrons() const {
    return pixelDistance() / calibration_.pixelsPerMicron;
}

std::string Measurement::label() const {
    const double microns = realDistanceMicrons();
    std::ostringstream oss;
    if (microns == std::floor(microns)) {
        oss << static_cast<long long>(microns);
    } else {
        oss << microns;
    }
    oss << " um";
    return oss.str();
}

} // namespace msapp
