// User journey:
//   As a teacher setting up a microscope, I capture a blank field once and
//   the app works out the colour gains that make it neutral, so every photo
//   taken on that microscope comes out without the Moticam A5's magenta
//   tint (ADR 0014).

#include "white_reference.hpp"
#include "test_support.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;

namespace {

// A green-deficient blank field like the A5's: B 200, G 120, R 180.
cv::Mat tintedField(int rows = 300, int cols = 400) {
    return cv::Mat(rows, cols, CV_8UC3, cv::Scalar(200, 120, 180));
}

template <typename Fn>
bool throwsInvalidArgument(Fn fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    std::cout << "== computeWhiteReferenceGains: neutralise a tinted blank field ==\n";
    {
        WhiteBalance g = computeWhiteReferenceGains(tintedField());
        check(nearlyEqual(g.green, 1.0, 1e-12), "normalised to green = 1, like the legacy gains");
        check(nearlyEqual(g.red, 120.0 / 180.0, 1e-9), "red gain = G/R (got " + std::to_string(g.red) + ")");
        check(nearlyEqual(g.blue, 120.0 / 200.0, 1e-9), "blue gain = G/B (got " + std::to_string(g.blue) + ")");

        cv::Mat corrected = applyWhiteBalanceGains(tintedField(), g);
        const cv::Vec3b px = corrected.at<cv::Vec3b>(10, 10);
        check(std::abs(px[0] - 120) <= 1 && std::abs(px[1] - 120) <= 1 && std::abs(px[2] - 120) <= 1,
              "corrected blank field is neutral grey");
    }
    {
        cv::Mat field = tintedField();
        cv::rectangle(field, cv::Rect(150, 110, 100, 80), cv::Scalar(255, 255, 255), cv::FILLED);
        WhiteBalance g = computeWhiteReferenceGains(field);
        check(nearlyEqual(g.red, 120.0 / 180.0, 1e-9) && nearlyEqual(g.blue, 120.0 / 200.0, 1e-9),
              "clipped pixels in the centre are ignored");
    }
    {
        cv::Mat field = tintedField();
        field.colRange(0, 60).setTo(cv::Scalar(30, 30, 30));  // dark vignette at the edge
        WhiteBalance g = computeWhiteReferenceGains(field);
        check(nearlyEqual(g.blue, 120.0 / 200.0, 1e-9), "edges (vignetting) are outside the measured centre");
    }

    std::cout << "\n== computeWhiteReferenceGains: refuses frames that aren't a usable blank field ==\n";
    check(throwsInvalidArgument([] { computeWhiteReferenceGains(cv::Mat(300, 400, CV_8UC3, cv::Scalar(10, 8, 9))); }),
          "too dark throws");
    check(throwsInvalidArgument([] { computeWhiteReferenceGains(cv::Mat(300, 400, CV_8UC3, cv::Scalar::all(255))); }),
          "fully clipped throws");
    check(throwsInvalidArgument([] { computeWhiteReferenceGains(cv::Mat(300, 400, CV_8UC3, cv::Scalar(5, 200, 200))); }),
          "a 40x gain (not a blank field) throws");
    check(throwsInvalidArgument([] { computeWhiteReferenceGains(cv::Mat(300, 400, CV_8UC1, cv::Scalar(120))); }),
          "grey input throws (no colour to balance)");

    std::cout << "\n== applyWhiteBalanceGains ==\n";
    {
        cv::Mat grey(4, 4, CV_8UC1, cv::Scalar(77));
        cv::Mat out = applyWhiteBalanceGains(grey, WhiteBalance{2.0, 1.0, 2.0});
        check(cv::countNonZero(out != grey) == 0, "grey images pass through unchanged");

        cv::Mat bgr(2, 2, CV_8UC3, cv::Scalar(200, 100, 50));
        cv::Mat same = applyWhiteBalanceGains(bgr, WhiteBalance{});
        check(cv::countNonZero(cv::Mat(same != bgr).reshape(1)) == 0, "identity gains change nothing");

        cv::Mat boosted = applyWhiteBalanceGains(bgr, WhiteBalance{2.0, 1.0, 2.0});
        check(boosted.at<cv::Vec3b>(0, 0) == cv::Vec3b(255, 100, 100), "gains saturate at 255 instead of wrapping");

        cv::Mat bgra(1, 1, CV_8UC4, cv::Scalar(100, 100, 100, 77));
        cv::Mat out4 = applyWhiteBalanceGains(bgra, WhiteBalance{2.0, 1.0, 0.5});
        check(out4.at<cv::Vec4b>(0, 0) == cv::Vec4b(50, 100, 200, 77), "BGRA: colour scaled, alpha untouched");
    }

    std::cout << "\n== whiteBalancedPreview: the live view shrinks first, then applies the gains ==\n";
    {
        const WhiteBalance gains{1.745, 1.0, 2.576};  // the legacy workstation's gains
        // Every level up to 98 in each channel: no gain clips any of them.
        cv::Mat dim(40, 99, CV_8UC3);
        for (int r = 0; r < dim.rows; ++r) {
            for (int c = 0; c < dim.cols; ++c) {
                dim.at<cv::Vec3b>(r, c) = cv::Vec3b(static_cast<uchar>(c), static_cast<uchar>((c + r) % 99),
                                                    static_cast<uchar>(98 - c));
            }
        }
        const cv::Mat full = applyWhiteBalanceGains(dim, gains);
        const cv::Mat same = whiteBalancedPreview(dim, gains, cv::Size());
        check(same.size() == dim.size() && cv::norm(same, full, cv::NORM_INF) <= 1.0,
              "at full size the lookup table gives applyWhiteBalanceGains' levels");

        const cv::Mat shrunk = whiteBalancedPreview(dim, gains, cv::Size(50, 50));
        check(shrunk.size() == cv::Size(50, 20), "shrunk to fit, aspect ratio kept");
        cv::Mat reference;
        cv::resize(full, reference, shrunk.size(), 0, 0, cv::INTER_AREA);
        const double gap = cv::norm(shrunk, reference, cv::NORM_INF);
        check(gap <= 3.0, "the same picture as shrinking the full-size result, up to rounding (got " +
                              std::to_string(gap) + " levels)");
    }
    {
        // Bright and dark pixels side by side: the gains clip some of them.
        const WhiteBalance gains{1.745, 1.0, 2.576};
        cv::Mat mixed(32, 256, CV_8UC3);
        for (int r = 0; r < mixed.rows; ++r) {
            for (int c = 0; c < mixed.cols; ++c) {
                const auto level = static_cast<uchar>((r + c) % 2 == 0 ? c : 255 - c);
                mixed.at<cv::Vec3b>(r, c) = cv::Vec3b(level, level, level);
            }
        }
        const cv::Mat shrunk = whiteBalancedPreview(mixed, gains, cv::Size(128, 128));
        cv::Mat reference;
        cv::resize(applyWhiteBalanceGains(mixed, gains), reference, shrunk.size(), 0, 0, cv::INTER_AREA);
        cv::Mat darker;
        cv::subtract(reference, shrunk, darker);  // saturates at 0 where the preview is brighter
        check(cv::norm(darker, cv::NORM_INF) <= 3.0,
              "where gains clip, clipping after the average is never darker than the capture would be");
    }
    {
        const cv::Mat grey(8, 8, CV_8UC1, cv::Scalar(77));
        const cv::Mat greyShown = whiteBalancedPreview(grey, WhiteBalance{2.0, 1.0, 2.0}, cv::Size(4, 4));
        check(greyShown.size() == cv::Size(4, 4) && cv::countNonZero(greyShown != 77) == 0,
              "grey: shrunk, levels unchanged");

        const cv::Mat bgr(2, 2, CV_8UC3, cv::Scalar(200, 100, 50));
        check(whiteBalancedPreview(bgr, WhiteBalance{}, cv::Size(1920, 1080)).data == bgr.data,
              "identity gains and a frame that already fits: no copy at all");

        const cv::Mat bgra(1, 1, CV_8UC4, cv::Scalar(100, 100, 100, 77));
        check(whiteBalancedPreview(bgra, WhiteBalance{2.0, 1.0, 0.5}, cv::Size()).at<cv::Vec4b>(0, 0) ==
                  cv::Vec4b(50, 100, 200, 77),
              "BGRA: colour scaled, alpha untouched");

        check(throwsInvalidArgument([] {
                  whiteBalancedPreview(cv::Mat(4, 4, CV_16UC3, cv::Scalar::all(1000)), WhiteBalance{}, cv::Size());
              }),
              "16-bit input is refused");
    }

    return summary();
}
