// User journey:
//   As a teacher setting up a workstation, I put the 20 µm mire under the
//   x40 objective, click one of its lines and the line ten periods further
//   along, and repeat until two readings agree within 0.25 % like the
//   legacy procedure; the app then stores x40's px/µm with the frame size
//   I measured at (ADR 0015), ready for every capture at x40.

#include "mire_calibration.hpp"
#include "workstation_config.hpp"
#include "test_support.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;

int main() {
    std::cout << "== One reading: pixels across N periods of 20 µm ==\n";
    {
        MireCalibration mire("x40", 3840, 2160);
        check(nearlyEqual(mire.addReading({100, 50}, {600, 50}, 10), 2.5),
              "500 px across 10 periods of 20 µm is 2.5 px/µm");
        check(nearlyEqual(mire.addReading({0, 0}, {300, 400}, 10), 2.5),
              "a slanted mire is measured along the line between the clicks");
        check(mire.readings().size() == 2, "both readings are kept");
        check(nearlyEqual(MireCalibration("x10", 3840, 2160, 10.0).addReading({0, 0}, {100, 0}, 5), 2.0),
              "another mire pitch can be given");
    }
    {
        MireCalibration mire("x40", 3840, 2160);
        check(!throwsWith<std::invalid_argument>([&] { mire.addReading({0, 0}, {500, 0}, 0); }).empty(),
              "zero periods is refused");
        check(!throwsWith<std::invalid_argument>([&] { mire.addReading({10, 10}, {10.5, 10}, 3); }).empty(),
              "two clicks on the same spot are refused");
        check(mire.readings().empty(), "a refused reading is not recorded");
    }
    {
        check(!throwsWith<std::invalid_argument>([] { MireCalibration("", 3840, 2160); }).empty(),
              "a Calibration needs a Grossissement");
        check(!throwsWith<std::invalid_argument>([] { MireCalibration("x40", 0, 0); }).empty(),
              "a Calibration needs the frame size it is measured at (ADR 0015)");
        check(!throwsWith<std::invalid_argument>([] { MireCalibration("x40", 3840, 2160, 0.0); }).empty(),
              "the mire pitch must be positive");
    }

    std::cout << "\n== Across the lines: clicks need not be level (#65) ==\n";
    {
        // Dark vertical lines every 31 px on a light background, like
        // tools/make_mire.py; and the same mire turned by 10 degrees.
        cv::Mat vertical(400, 600, CV_8UC1, cv::Scalar(215));
        for (int x = 15; x < vertical.cols; x += 31) {
            cv::line(vertical, {x, 0}, {x, vertical.rows - 1}, cv::Scalar(45), 3);
        }
        const auto upright = mireLineNormal(vertical);
        check(upright && nearlyEqual((*upright)[0], 1.0, 1e-3) && nearlyEqual((*upright)[1], 0.0, 1e-3),
              "vertical lines: the direction across them is horizontal");

        cv::Mat tilted;
        const cv::Mat turn = cv::getRotationMatrix2D(cv::Point2f(300, 200), 10.0, 1.0);
        cv::warpAffine(vertical, tilted, turn, vertical.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT);
        const auto askew = mireLineNormal(tilted);
        const double angle = askew ? std::atan2((*askew)[1], (*askew)[0]) * 180.0 / CV_PI : 999.0;
        check(askew && std::abs(std::abs(angle) - 10.0) < 1.0, "a mire set 10 degrees askew is found 10 degrees askew");

        cv::Mat noise(400, 600, CV_8UC1);
        cv::randu(noise, 0, 255);
        check(!mireLineNormal(cv::Mat(400, 600, CV_8UC1, cv::Scalar(128))) && !mireLineNormal(noise),
              "a blank or noisy image has no line direction: clicks are measured as they are");
        cv::Mat colour;
        cv::cvtColor(vertical, colour, cv::COLOR_GRAY2BGR);
        check(mireLineNormal(colour).has_value(), "a colour still works too");

        MireCalibration mire("x5", 3840, 2160);
        mire.setLineNormal(upright);
        check(nearlyEqual(mire.addReading({100, 100}, {2580, 900}, 20), 2480.0 / 400.0),
              "with vertical lines, clicks 800 px apart in height still read 2480 px across 20 periods");
        check(nearlyEqual(mire.measuredPixels({100, 100}, {100, 900}), 0.0) &&
                  !throwsWith<std::invalid_argument>([&] { mire.addReading({100, 100}, {100, 900}, 5); }).empty(),
              "two clicks on the same line, one above the other, measure nothing and are refused");
        mire.setLineNormal(cv::Vec2d(3.0, 4.0));
        check(nearlyEqual(mire.measuredPixels({0, 0}, {300, 400}), 500.0), "a direction need not be unit length");
        check(!throwsWith<std::invalid_argument>([&] { mire.setLineNormal(cv::Vec2d(0, 0)); }).empty(),
              "a zero direction is refused");
        mire.setLineNormal(std::nullopt);
        check(nearlyEqual(mire.measuredPixels({0, 0}, {300, 400}), 500.0) &&
                  nearlyEqual(mire.measuredPixels({0, 0}, {300, 0}), 300.0),
              "without a direction the clicks' straight distance counts, as before");
    }

    std::cout << "\n== Repeat until two readings agree within 0.25 % ==\n";
    {
        MireCalibration mire("x40", 3840, 2160);
        check(!mire.agreed() && !mire.lastDifference(), "nothing agrees before two readings");
        check(!throwsWith<std::logic_error>([&] { mire.result(); }).empty(), "no result before agreement");

        mire.addReading({0, 0}, {2000, 0}, 40);  // 2.5 px/µm
        mire.addReading({0, 0}, {2008, 0}, 40);  // 2.51: 0.40 % off
        check(!mire.agreed() && nearlyEqual(*mire.lastDifference(), 0.01 / 2.505),
              "readings 0.40 % apart don't agree: measure again");

        mire.addReading({0, 0}, {2009.6, 0}, 40);  // 2.512: 0.08 % from 2.51
        check(mire.agreed(), "the newest two agree within 0.25 %");
        const Calibration x40 = mire.result();
        check(x40.grossissement == "x40" && nearlyEqual(x40.pixelsPerMicron, 2.511) && x40.frameWidth == 3840 &&
                  x40.frameHeight == 2160,
              "the Calibration is the mean of the two agreeing readings, at the frame size measured");
        check(x40.fitsFrame(3840, 2160) && !x40.fitsFrame(1024, 768), "and holds only at that frame size");

        mire.addReading({0, 0}, {2100, 0}, 40);
        check(!mire.agreed(), "a later reading that disagrees reopens the calibration");
        mire.reset();
        check(mire.readings().empty() && !mire.agreed(), "reset starts over");
    }
    {
        MireCalibration mire("x40", 3840, 2160);
        mire.addReading({0, 0}, {2000, 0}, 40);   // 2.5
        mire.addReading({0, 0}, {2004.8, 0}, 40); // 2.506: 0.24 %
        check(mire.agreed(), "0.24 % apart is within tolerance");
    }

    std::cout << "\n== Into the workstation config ==\n";
    {
        MireCalibration mire("x40", 3840, 2160);
        mire.addReading({0, 0}, {2000, 0}, 40);
        mire.addReading({0, 0}, {2001, 0}, 40);
        WorkstationConfig config;
        config.setCalibration(mire.result());
        const auto stored = config.calibrationForFrame("x40", 3840, 2160);
        check(stored && nearlyEqual(stored->pixelsPerMicron, (2000.0 + 2001.0) / 2.0 / 800.0),
              "the wizard's result is stored as x40's Calibration");
    }

    return summary();
}
