// User journey (TDD Step 1):
//   As an operator, I want to select two points on a Vue and see the
//   real-world distance between them, so I can measure a sample feature
//   without permanently marking the photo (CONTEXT.md: Measurement is
//   read-only, unlike Échelle which mutates the photo).
//
// This file is written against measurement.hpp before it exists —
// expected to fail to compile (RED), per the TDD workflow's allowed
// compile-time RED path.

#include "measurement.hpp"
#include "calibration.hpp"
#include "echelle_treatment.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  [PASS] " << description << "\n";
    } else {
        std::cout << "  [FAIL] " << description << "\n";
        ++g_failures;
    }
}

bool nearlyEqual(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

} // namespace

int main() {
    Calibration calib{"x40", 2.5}; // 2.5 px per micron

    std::cout << "== Measurement: pixel distance ==\n";
    {
        // Classic 3-4-5 right triangle -> exact distance of 5px, no
        // floating-point rounding ambiguity to worry about.
        Measurement m({0.0, 0.0}, {3.0, 4.0}, calib);
        check(nearlyEqual(m.pixelDistance(), 5.0), "3-4-5 triangle gives exact pixel distance of 5");
    }
    {
        Measurement m({10.0, 20.0}, {50.0, 20.0}, calib);
        check(nearlyEqual(m.pixelDistance(), 40.0), "horizontal-only distance");
    }
    {
        Measurement m({10.0, 20.0}, {10.0, 70.0}, calib);
        check(nearlyEqual(m.pixelDistance(), 50.0), "vertical-only distance");
    }
    {
        Measurement m({15.5, 8.0}, {15.5, 8.0}, calib);
        check(nearlyEqual(m.pixelDistance(), 0.0), "same point twice gives zero distance");
    }

    std::cout << "\n== Measurement: real-world conversion via Calibration ==\n";
    {
        // 5px at 2.5 px/um -> 2um real distance.
        Measurement m({0.0, 0.0}, {3.0, 4.0}, calib);
        check(nearlyEqual(m.realDistanceMicrons(), 2.0), "5px at 2.5px/um converts to 2um");
    }
    {
        Calibration highMag{"x100", 10.0};
        Measurement m({0.0, 0.0}, {25.0, 0.0}, highMag);
        check(nearlyEqual(m.realDistanceMicrons(), 2.5), "25px at 10px/um converts to 2.5um");
    }

    std::cout << "\n== Measurement: invalid Calibration is an error, not a silent wrong answer ==\n";
    {
        Calibration zero{"broken", 0.0};
        bool threw = false;
        try {
            Measurement m({0.0, 0.0}, {10.0, 0.0}, zero);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "zero pixels-per-micron throws invalid_argument (can't divide by zero calibration)");
    }
    {
        Calibration negative{"broken", -1.0};
        bool threw = false;
        try {
            Measurement m({0.0, 0.0}, {10.0, 0.0}, negative);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "negative pixels-per-micron throws invalid_argument");
    }

    std::cout << "\n== Calibration: only holds at the frame size it was measured at (ADR 0015) ==\n";
    {
        const Calibration at4k{"x40", 2.5, 3840, 2160};
        check(at4k.fitsFrame(3840, 2160), "same frame size fits");
        check(!at4k.fitsFrame(1024, 768) && !at4k.fitsFrame(1920, 1080),
              "a fallback mode does not fit, whether or not its aspect ratio matches");
        check(calib.fitsFrame(1024, 768) && !calib.hasFrameSize(),
              "a Calibration without a frame size (pre-ADR 0015) is accepted unverified");

        std::string message;
        try {
            at4k.requireFitsFrame(1024, 768, "test");
        } catch (const std::runtime_error& e) {
            message = e.what();
        }
        check(message.find("3840x2160") != std::string::npos && message.find("1024x768") != std::string::npos,
              "a mismatch is refused, naming both sizes (got '" + message + "')");

        const Calibration reloaded = Calibration::fromJson(at4k.toJson());
        check(reloaded.grossissement == "x40" && nearlyEqual(reloaded.pixelsPerMicron, 2.5) &&
                  reloaded.frameWidth == 3840 && reloaded.frameHeight == 2160,
              "frame size round-trips through JSON");
        const Calibration old = Calibration::fromJson({{"grossissement", "x40"}, {"pixels_per_micron", 2.5}});
        check(!old.hasFrameSize(), "JSON written before ADR 0015 (no frame size) still loads, as unknown");
    }

    std::cout << "\n== Échelle: refuses a Calibration measured at another frame size ==\n";
    {
        const cv::Mat image(100, 200, CV_8UC3, cv::Scalar::all(128));
        EchelleTreatment matching(Calibration{"x40", 2.5, 200, 100});
        check(!matching.apply(image).empty(), "matching frame size renders");
        EchelleTreatment unknown(calib);
        check(!unknown.apply(image).empty(), "unknown frame size renders (pre-ADR 0015 Échelle params)");

        bool threw = false;
        try {
            EchelleTreatment(Calibration{"x40", 2.5, 400, 200}).apply(image);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "a bar from a Calibration measured at 400x200 is not drawn on a 200x100 image");
    }

    std::cout << "\n== Measurement: display label ==\n";
    {
        Measurement m({0.0, 0.0}, {3.0, 4.0}, calib);
        check(m.label() == "2 um", "label() formats a whole-number result as '2 um' (got '" + m.label() + "')");
    }
    {
        Calibration highMag{"x100", 10.0};
        Measurement m({0.0, 0.0}, {25.0, 0.0}, highMag);
        check(m.label() == "2.5 um", "label() formats a fractional result as '2.5 um' (got '" + m.label() + "')");
    }

    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
