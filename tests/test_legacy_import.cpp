// User journey:
//   As a teacher switching to the new app, I want last year's TP structure
//   (GMP1.txt, CTTM.txt, ...) and archived views (TP1Ech2Vue1.tif) brought
//   over with their names and calibration intact, instead of retyping
//   Échantillon names or re-measuring old photos against the mire.
//
// tests/fixtures/legacy/*.txt are byte-for-byte copies of real legacy
// catalog files. TIFF checks on real archives need the legacy data folder
// (argv[1] or MSAPP_LEGACY_DATA_DIR) and are skipped when it is absent.

#include "legacy_catalog.hpp"
#include "legacy_view.hpp"

#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

bool nearlyEqual(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

fs::path legacyDataDir(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* env = std::getenv("MSAPP_LEGACY_DATA_DIR")) return env;
    return {};
}

// Payload of tag 0xF000 copied from CTTM1Ech1Vue1.tif (x50 objective).
const std::vector<unsigned char> kRealBlob = {
    0xe0, 0x07, 0xcd, 0xcd, 0x54, 0x00, 0x00, 0x00,                                      // header, length 84
    0x00, 0x02, 0x19, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x42,              // 0x200 = 50.0f
    0x07, 0x02, 0x19, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x82, 0x61, 0x46,              // 0x207 = 14432.5f
    0x03, 0x02, 0x19, 0x00, 0x04, 0x00, 0x00, 0x00, 0x26, 0x93, 0x49, 0x44,              // 0x203 = 806.2992f
    0x04, 0x02, 0x19, 0x00, 0x04, 0x00, 0x00, 0x00, 0x26, 0x93, 0x49, 0x44,              // 0x204 = 806.2992f
    0x05, 0x02, 0x19, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,                          // 0x205 = 2
    0x01, 0x02, 0x19, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x00,                          // 0x201 = 10
    0x00, 0x7d, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,                                      // end
};

// Minimal little-endian TIFF: header, one IFD with a single tag, payload.
std::string tiffWithTag(std::uint16_t tag, const std::vector<unsigned char>& payload) {
    std::string bytes;
    auto put16 = [&](std::uint16_t v) { bytes += char(v & 0xFF); bytes += char(v >> 8); };
    auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) bytes += char((v >> (8 * i)) & 0xFF); };
    bytes += "II";
    put16(42);
    put32(8);           // IFD offset
    put16(1);           // one entry
    put16(tag);
    put16(1);           // BYTE
    put32(static_cast<std::uint32_t>(payload.size()));
    put32(26);          // payload offset: 8 + 2 + 12 + 4
    put32(0);           // no next IFD
    bytes.append(payload.begin(), payload.end());
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path fixtures = fs::path(MSAPP_SOURCE_DIR) / "tests" / "fixtures" / "legacy";

    std::cout << "== Legacy Department catalog: GMP1.txt ==\n";
    {
        LegacyCatalog c = loadLegacyDepartmentCatalog(fixtures / "GMP1.txt");
        check(c.department == "GMP1", "department comes from the file name");
        check(c.version == 4, "version 4 header parsed");
        check(c.tps.size() == 7, "always 7 TP slots, as legacy");

        const LegacyTp& tp1 = c.tps[0];
        check(tp1.slot == 1 && tp1.name == "TP 1: Aciers", "TP1 name (got '" + tp1.name + "')");
        check(tp1.printCode == -1 && tp1.flags == 16, "TP1 print code -1, flags 16");
        check(tp1.echantillons.size() == 6, "TP1 keeps its 6 named Échantillons, drops blank slots");
        check(tp1.echantillons[0].slot == 1 && tp1.echantillons[0].name == "A5 : C10", "slot 1 = 'A5 : C10'");
        check(tp1.echantillons[3].slot == 4 && tp1.echantillons[3].name == "A8 : C150", "slot 4 = 'A8 : C150'");
        check(tp1.instructions.find("imprim\xC3\xA9" "e") != std::string::npos,
              "Windows-1252 'é' arrives as UTF-8");

        const LegacyTp& tp2 = c.tps[1];
        check(tp2.echantillons.size() == 7 && tp2.echantillons[6].name == "\xC3\xA0 Identifier 2",
              "TP2 slot 7 '\xC3\xA0 Identifier 2' decoded from Windows-1252");
        check(c.tps[3].name == "TP4 : Traction" && c.tps[3].echantillons.size() == 2,
              "TP4 Traction has 2 Échantillons");
        check(c.tps[5].name.empty() && c.tps[5].echantillons.size() == 6,
              "unnamed TP slot 6 still keeps its Échantillons E1..E6");
    }

    std::cout << "\n== Legacy Department catalog: CTTM.txt ==\n";
    {
        LegacyCatalog c = loadLegacyDepartmentCatalog(fixtures / "CTTM.txt");
        check(c.department == "CTTM" && c.tps.size() == 7, "CTTM catalog has 7 TP slots");
        check(c.tps[0].name == "1" && c.tps[0].echantillons.size() == 8, "CTTM TP '1' has E1..E8");
        check(c.tps[1].echantillons.empty(), "CTTM TP '2' has no Échantillons");
    }

    std::cout << "\n== Legacy Department catalog: bad input is an error ==\n";
    {
        bool threw = false;
        try {
            std::istringstream truncated("       4 0\n//user\n//Nouveau TP\nTP 1\n");
            parseLegacyDepartmentCatalog(truncated, "X");
        } catch (const std::runtime_error& e) {
            threw = std::string(e.what()).find("line") != std::string::npos;
        }
        check(threw, "truncated file throws, naming the line");
        threw = false;
        try {
            std::istringstream v2("       2 0\n");
            parseLegacyDepartmentCatalog(v2, "X");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "version 2 (DATA_METAL.txt) is rejected, not misparsed");
    }

    std::cout << "\n== Legacy view file names ==\n";
    {
        auto tp = parseLegacyViewFileName("TP3Ech2Vue1.tif");
        check(tp && tp->prefix == "TP" && tp->tp == 3 && tp->echantillon == 2 && tp->vue == 1, "TP3Ech2Vue1.tif");
        auto cttm = parseLegacyViewFileName("CTTM1Ech4Vue2.tif");
        check(cttm && cttm->prefix == "CTTM" && cttm->tp == 1 && cttm->echantillon == 4 && cttm->vue == 2,
              "CTTM1Ech4Vue2.tif");
        check(!parseLegacyViewFileName("vue_001.tiff"), "new-style names are not legacy views");
    }

    std::cout << "\n== Legacy view metadata (tag 0xF000) ==\n";
    {
        std::istringstream tiff(tiffWithTag(0xF000, kRealBlob));
        auto meta = parseLegacyViewMetadata(tiff);
        check(meta.has_value(), "tag 0xF000 found");
        if (meta) {
            check(nearlyEqual(meta->grossissement, 50.0), "objective x50");
            check(nearlyEqual(meta->pixelsPerMm, 14432.5), "14432.5 px/mm on the sample");
            const Calibration cal = meta->calibration();
            check(cal.grossissement == "x50" && nearlyEqual(cal.pixelsPerMicron, 14.4325),
                  "Calibration{x50, 14.4325 px/um} (got " + cal.grossissement + ", " +
                      std::to_string(cal.pixelsPerMicron) + ")");
            check(nearlyEqual(meta->fields.at(0x203), 806.2992) && meta->fields.at(0x205) == 2 &&
                      meta->fields.at(0x201) == 10,
                  "unexplained fields are kept, not dropped");
        }
        std::istringstream other(tiffWithTag(0x0131, {'a', 'p', 'p', 0}));
        check(!parseLegacyViewMetadata(other).has_value(), "TIFF without tag 0xF000 gives no metadata");

        bool threw = false;
        try {
            std::istringstream png("\x89PNG\r\n");
            parseLegacyViewMetadata(png);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "non-TIFF input throws");
    }

    std::cout << "\n== Legacy archive import: real CTTM views ==\n";
    const fs::path dataDir = legacyDataDir(argc, argv);
    if (dataDir.empty() || !fs::exists(dataDir / "CTTM1Ech1Vue1.tif")) {
        std::cout << "  [SKIP] legacy data folder not given (argv[1] or MSAPP_LEGACY_DATA_DIR)\n";
    } else {
        auto x50 = readLegacyViewMetadata(dataDir / "CTTM1Ech1Vue1.tif");
        auto x20 = readLegacyViewMetadata(dataDir / "CTTM1Ech1Vue2.tif");
        check(x50 && nearlyEqual(x50->grossissement, 50.0) && nearlyEqual(x50->pixelsPerMm, 14432.5),
              "CTTM1Ech1Vue1.tif: x50, 14432.5 px/mm");
        check(x20 && nearlyEqual(x20->grossissement, 20.0) && nearlyEqual(x20->pixelsPerMm, 5773.0),
              "CTTM1Ech1Vue2.tif: x20, 5773 px/mm (= 288.65 px/mm x 20)");

        const fs::path outDir = fs::temp_directory_path() / "msapp_legacy_import_test";
        fs::remove_all(outDir);
        fs::create_directories(outDir);
        LegacyViewImport imported = importLegacyView(dataDir / "CTTM1Ech1Vue1.tif", outDir / "imported.tiff");
        cv::Mat legacyPixels = cv::imread((dataDir / "CTTM1Ech1Vue1.tif").string(), cv::IMREAD_UNCHANGED);
        cv::Mat diff;
        cv::absdiff(imported.vue.original(), legacyPixels, diff);
        check(cv::countNonZero(diff.reshape(1)) == 0, "imported Vue is pixel-identical to the legacy view");
        check(imported.name && imported.name->echantillon == 1 && imported.name->vue == 1,
              "import reports TP/Échantillon/Vue from the file name");
        check(imported.metadata && imported.metadata->calibration().grossissement == "x50",
              "import reports the legacy calibration");
        fs::remove_all(outDir);
    }

    std::cout << "\n== " << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED")
              << " ==\n";
    return g_failures == 0 ? 0 : 1;
}
