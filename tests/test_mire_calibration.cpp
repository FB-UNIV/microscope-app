// User journey:
//   As a teacher setting up a workstation, I put the 20 µm mire under the
//   x40 objective, click one of its lines and the line ten periods further
//   along, and repeat until two readings agree within 0.25 % like the
//   legacy procedure; the app then stores x40's px/µm with the frame size
//   I measured at (ADR 0015), ready for every capture at x40.

#include "mire_calibration.hpp"
#include "workstation_config.hpp"
#include "test_support.hpp"

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
