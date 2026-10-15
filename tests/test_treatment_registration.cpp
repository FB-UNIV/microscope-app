// User journey:
//   As a teacher opening last week's photos in a tool that only browses Vues
//   (no editing, so it never names a Treatment class), I want every saved
//   Levels / Échelle / phase overlay to render, not a "unknown treatment
//   type" error.
//
// Deliberately includes no concrete Treatment header: microscope_core is a
// static library, so this binary pulls in only the object files it reaches
// through Vue and TreatmentFactory. Every built-in treatment must still be
// known to the factory.

#include "treatment_factory.hpp"
#include "vue.hpp"

#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace msapp;
namespace fs = std::filesystem;

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

// Sidecar params for each built-in type, written as a saved Vue would have them.
const nlohmann::json kBuiltins = {
    {"levels", {{"black_point", 90}, {"white_point", 180}}},
    {"echelle", {{"calibration", {{"grossissement", "x40"}, {"pixels_per_micron", 2.5}}}}},
    {"phase_overlay", {{"threshold", 128}}},
};

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_treatment_registration_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::cout << "== Vue::load with only Vue linked in: every built-in treatment is known ==\n";
    {
        const fs::path imagePath = dir / "vue.png";
        cv::imwrite(imagePath.string(), cv::Mat(48, 64, CV_8UC3, cv::Scalar(90, 120, 150)));
        nlohmann::json treatments = nlohmann::json::array();
        for (const auto& [type, params] : kBuiltins.items()) {
            treatments.push_back({{"type", type}, {"params", params}});
        }
        std::ofstream(imagePath.string() + ".json") << nlohmann::json{{"treatments", treatments}}.dump();

        try {
            Vue vue = Vue::load(imagePath);
            check(vue.treatmentCount() == kBuiltins.size(),
                  "levels, echelle and phase_overlay all rebuilt from the sidecar");
            check(!vue.render().empty(), "the reloaded Vue renders");
        } catch (const std::exception& e) {
            check(false, std::string("Vue::load threw: ") + e.what());
        }
    }

    std::cout << "\n== TreatmentFactory: each type is registered under its own type() ==\n";
    for (const auto& [type, params] : kBuiltins.items()) {
        try {
            check(TreatmentFactory::instance().create(type, params)->type() == type,
                  "'" + type + "' creates a treatment whose type() is '" + type + "'");
        } catch (const std::exception& e) {
            check(false, "'" + type + "' threw: " + e.what());
        }
    }

    fs::remove_all(dir);
    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
