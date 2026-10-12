// User journey:
//   As a student in Mesures Physiques, I click the two diagonals of a
//   Vickers indent on a Vue and read its hardness, computed exactly as the
//   legacy Metal app did (HV0.5, coefficient recovered from the binary).

#include "vickers.hpp"
#include "calibration.hpp"
#include "measurement.hpp"

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

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

// The legacy formula exactly as decompiled (0x44aad0), d in um.
double legacyHV(double meanDiagonalMicrons) {
    return 0.927045 / (meanDiagonalMicrons * meanDiagonalMicrons) * 1000.0 * 1000.0;
}

} // namespace

int main() {
    std::cout << "== Vickers: matches the legacy HV0.5 formula ==\n";
    {
        VickersResult r = vickersHardness(50.0, 50.0);
        check(nearlyEqual(r.hardness, legacyHV(50.0)),
              "50um indent at 0.5kgf gives legacy value " + std::to_string(legacyHV(50.0)) + " (got " +
                  std::to_string(r.hardness) + ")");
        check(nearlyEqual(r.hardness, 370.818), "50um indent = 370.818 HV0.5");
    }
    {
        VickersResult r = vickersHardness(40.0, 44.0);
        check(nearlyEqual(r.meanDiagonalMicrons, 42.0), "mean diagonal is the average of both");
        check(nearlyEqual(r.hardness, legacyHV(42.0)), "unequal diagonals use the mean, as legacy");
    }

    std::cout << "\n== Vickers: load is a parameter, legacy default 0.5 kgf ==\n";
    {
        VickersResult half = vickersHardness(50.0, 50.0);
        VickersResult ten = vickersHardness(50.0, 50.0, 10.0);
        check(nearlyEqual(half.loadKgf, 0.5), "default load is 0.5 kgf");
        check(nearlyEqual(ten.hardness, half.hardness * 20.0), "HV scales linearly with load");
        check(half.label() == "370.8 HV0.5", "label uses ISO notation (got '" + half.label() + "')");
    }

    std::cout << "\n== Vickers: from Measurements on a calibrated Vue ==\n";
    {
        // Legacy calibration 288.65 px/mm at 1x -> 14.4325 px/um at x50.
        Calibration x50{"x50", 14.4325};
        Measurement d1({0.0, 0.0}, {721.625, 0.0}, x50);  // 50 um
        Measurement d2({0.0, 0.0}, {0.0, 721.625}, x50);  // 50 um
        VickersResult r = vickersHardness(d1, d2);
        check(nearlyEqual(r.diagonal1Microns, 50.0) && nearlyEqual(r.diagonal2Microns, 50.0),
              "pixel diagonals converted through the Vue's Calibration");
        check(nearlyEqual(r.hardness, legacyHV(50.0)), "same HV as entering microns directly");
    }

    std::cout << "\n== Vickers: invalid input is an error ==\n";
    {
        bool threw = false;
        try {
            vickersHardness(0.0, 50.0);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "zero diagonal throws invalid_argument");
        threw = false;
        try {
            vickersHardness(50.0, 50.0, -1.0);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "negative load throws invalid_argument");
    }

    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
