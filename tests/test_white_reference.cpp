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

    return summary();
}
