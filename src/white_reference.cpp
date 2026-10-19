#include "white_reference.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace msapp {

namespace {

constexpr int kClippedLevel = 250;
constexpr double kMinGreenMean = 20.0;
constexpr double kMinGain = 0.25;
constexpr double kMaxGain = 4.0;

} // namespace

WhiteBalance computeWhiteReferenceGains(const cv::Mat& blankFieldBgr) {
    if (blankFieldBgr.type() != CV_8UC3) {
        throw std::invalid_argument("computeWhiteReferenceGains: expected an 8-bit BGR frame");
    }
    const cv::Rect centre(blankFieldBgr.cols / 4, blankFieldBgr.rows / 4, blankFieldBgr.cols / 2,
                          blankFieldBgr.rows / 2);
    const cv::Mat region = blankFieldBgr(centre);

    cv::Mat unclipped;  // 255 where every channel is below the clip level
    cv::inRange(region, cv::Scalar(0, 0, 0), cv::Scalar(kClippedLevel - 1, kClippedLevel - 1, kClippedLevel - 1),
                unclipped);
    if (cv::countNonZero(unclipped) < static_cast<int>(region.total() / 10)) {
        throw std::invalid_argument("computeWhiteReferenceGains: blank field is mostly clipped — lower the exposure");
    }

    const cv::Scalar mean = cv::mean(region, unclipped);
    if (mean[1] < kMinGreenMean) {
        throw std::invalid_argument("computeWhiteReferenceGains: blank field too dark (mean green " +
                                    std::to_string(mean[1]) + ") — raise the exposure or the lamp");
    }

    WhiteBalance gains{mean[1] / mean[2], 1.0, mean[1] / mean[0]};
    for (double g : {gains.red, gains.blue}) {
        if (g < kMinGain || g > kMaxGain) {
            throw std::invalid_argument("computeWhiteReferenceGains: gain " + std::to_string(g) +
                                        " outside [0.25, 4] — is this really a blank field?");
        }
    }
    return gains;
}

cv::Mat applyWhiteBalanceGains(const cv::Mat& image, const WhiteBalance& gains) {
    if (image.channels() == 1 || gains.isIdentity()) {
        return image.clone();
    }
    if (image.depth() != CV_8U || image.channels() < 3) {
        throw std::invalid_argument("applyWhiteBalanceGains: expected 8-bit grey, BGR or BGRA");
    }
    cv::Mat out;
    const cv::Scalar perChannel = image.channels() == 4 ? cv::Scalar(gains.blue, gains.green, gains.red, 1.0)
                                                        : cv::Scalar(gains.blue, gains.green, gains.red);
    cv::multiply(image, perChannel, out);  // saturating for 8-bit
    return out;
}

cv::Mat whiteBalancedPreview(const cv::Mat& image, const WhiteBalance& gains, cv::Size fitWithin) {
    const int channels = image.channels();
    if (image.depth() != CV_8U || (channels != 1 && channels != 3 && channels != 4)) {
        throw std::invalid_argument("whiteBalancedPreview: expected 8-bit grey, BGR or BGRA");
    }
    cv::Mat shown = image;
    if (!fitWithin.empty() && (image.cols > fitWithin.width || image.rows > fitWithin.height)) {
        const double scale = std::min(static_cast<double>(fitWithin.width) / image.cols,
                                      static_cast<double>(fitWithin.height) / image.rows);
        // The size, not the factor: the whole image then maps onto the whole
        // output, even when the factor doesn't divide both sides evenly.
        const cv::Size shrunk(std::max(1, cvRound(image.cols * scale)), std::max(1, cvRound(image.rows * scale)));
        cv::resize(image, shown, shrunk, 0.0, 0.0, cv::INTER_AREA);
    }
    if (channels == 1 || gains.isIdentity()) {
        return shown;
    }
    // One table entry per level and channel, rounded and saturated like
    // applyWhiteBalanceGains; alpha maps to itself.
    cv::Mat table(1, 256, CV_8UC(channels));
    uchar* entry = table.ptr<uchar>();
    for (int level = 0; level < 256; ++level, entry += channels) {
        entry[0] = cv::saturate_cast<uchar>(level * gains.blue);
        entry[1] = cv::saturate_cast<uchar>(level * gains.green);
        entry[2] = cv::saturate_cast<uchar>(level * gains.red);
        if (channels == 4) entry[3] = static_cast<uchar>(level);
    }
    cv::Mat out;
    cv::LUT(shown, table, out);
    return out;
}

} // namespace msapp
