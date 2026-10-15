// User journey:
//   As a student, every photo I capture lands in my Échantillon's folder
//   under a name nobody else's capture can take, even when two of us click
//   in the same second; before any TP exists on this workstation, my photos
//   still save, into Unsorted (ADR 0008). As a teacher, I can list the Vues
//   of one Échantillon in the order they were taken.

#include "vue.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& path) {
    std::ofstream(path, std::ios::binary) << "x";
}

std::string folderOf(const std::string& name) {
    return vueFolderName(name);
}

bool refusesFolderName(const std::string& name) {
    return !throwsWith<std::invalid_argument>([&] { vueFolderName(name); }).empty();
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_vue_store_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::cout << "== vueFolderName: names a teacher types, made safe as folders ==\n";
    {
        check(folderOf("TP 1: Aciers") == "TP 1_ Aciers" && folderOf("A5 : C10") == "A5 _ C10",
              "a colon (forbidden on Windows) becomes '_'");
        check(folderOf("a/b\\c<d>e\"f|g?h*i") == "a_b_c_d_e_f_g_h_i", "every character Windows forbids becomes '_'");
        check(folderOf(std::string("tab\there")) == "tab_here", "control characters become '_'");
        check(folderOf("  Ac1. . ") == "Ac1", "leading spaces and trailing spaces and dots are dropped");
        check(folderOf("Échantillon été") == "Échantillon été", "accented names are kept as they are");
        check(folderOf("CON") == "CON_" && folderOf("nul") == "nul_" && folderOf("com3.txt") == "com3.txt_" &&
                  folderOf("LPT9") == "LPT9_",
              "Windows device names get a trailing '_'");
        check(folderOf("CONSOLE") == "CONSOLE" && folderOf("COM0") == "COM0", "names that only start like one are kept");

        std::string accents;
        for (int i = 0; i < 100; ++i) accents += "é";  // 2 bytes each
        const std::string cut = folderOf(accents);
        check(cut.size() == 64 && cut == accents.substr(0, 64), "long names are cut to 64 bytes");
        std::string odd = "x";
        odd += accents;
        check(folderOf(odd).size() == 63, "the cut never splits a UTF-8 character");

        check(refusesFolderName("") && refusesFolderName("   ") && refusesFolderName("..."),
              "a name with nothing usable is refused");
    }

    std::cout << "\n== VueStore::folder: <root>/<Department>/<TP>/<Échantillon> ==\n";
    const VueStore store(dir / "vues");
    const VueLocation aciers{"GMP", "TP 1: Aciers", "A5 : C10"};
    {
        check(store.folder(aciers) == dir / "vues" / "GMP" / "TP 1_ Aciers" / "A5 _ C10",
              "an Échantillon's Vues live under Department / TP / Échantillon");
        check(store.folder(VueLocation::unsorted()) == dir / "vues" / "Unsorted",
              "no Department, TP or Échantillon: the Unsorted bucket (ADR 0008)");
        check(store.folder({"unsorted", "TP", "E"}) == dir / "vues" / "unsorted_" / "TP" / "E",
              "a Department named Unsorted can't share the bucket's folder");
        check(!throwsWith<std::invalid_argument>([&] { store.folder({"GMP", "", "A5"}); }).empty(),
              "a partial location is refused, not filed half-way");
    }

    std::cout << "\n== newVuePath: unique, immutable paths ==\n";
    {
        const fs::path first = store.newVuePath(aciers);
        check(fs::is_directory(store.folder(aciers)), "the Échantillon's folder is created");
        check(first.parent_path() == store.folder(aciers) && first.extension() == ".tiff",
              "the path is a .tiff in the Échantillon's folder");
        const std::string name = first.filename().string();
        check(name.size() == std::string("20261015-154353-123Z_0123456789abcdef.tiff").size() &&
                  name.substr(8, 1) == "-" && name.substr(19, 2) == "Z_",
              "named <UTC time>_<random>.tiff");
        check(!fs::exists(first), "the path is free");

        // Captures in a tight loop: well within one second, most within one
        // millisecond. Each path is taken before asking for the next.
        std::set<fs::path> seen;
        bool allFree = true;
        for (int i = 0; i < 300; ++i) {
            const fs::path path = store.newVuePath(aciers);
            allFree = allFree && !fs::exists(path);
            seen.insert(path);
            touch(path);
        }
        check(seen.size() == 300 && allFree, "300 captures in the same second never collide");
    }

    std::cout << "\n== Capturing and listing ==\n";
    {
        const VueLocation ech{"CHIMIE", "TP 2", "Ac1: XC10"};
        const cv::Mat frame(48, 64, CV_8UC3, cv::Scalar(30, 120, 200));
        const Vue a = Vue::capture(frame, store.newVuePath(ech), CaptureInfo{});
        const Vue b = Vue::capture(frame, store.newVuePath(ech), CaptureInfo{});
        check(fs::exists(a.sidecarPath()) && fs::exists(b.sidecarPath()), "captured Vues have their sidecars");

        const auto vues = store.listVues(ech);
        check(vues.size() == 2 && ((vues[0] == a.imagePath() && vues[1] == b.imagePath()) ||
                                   (vues[0] == b.imagePath() && vues[1] == a.imagePath())),
              "listVues returns the two images, not their sidecars");
        const Vue reloaded = Vue::load(vues[0]);
        check(reloaded.original().size() == frame.size(), "a listed Vue loads back as a TIFF");

        const Vue unsorted = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        check(store.listVues(VueLocation::unsorted()) == std::vector<fs::path>{unsorted.imagePath()},
              "with no TP at all, capture still works, into Unsorted");
        check(store.listVues({"GMP", "TP 9", "nothing yet"}).empty(), "an Échantillon with no Vue lists nothing");
    }
    {
        const VueLocation ech{"GMP", "TP 3", "Ordered"};
        const fs::path folder = store.folder(ech);
        fs::create_directories(folder);
        touch(folder / "20261015-090000-500Z_bbbbbbbbbbbbbbbb.tiff");
        touch(folder / "20261014-235959-999Z_ffffffffffffffff.tiff");
        touch(folder / "20261015-090000-500Z_aaaaaaaaaaaaaaaa.tiff");
        touch(folder / "20261014-235959-999Z_ffffffffffffffff.tiff.json");
        touch(folder / "20261014-235959-999Z_ffffffffffffffff.tiff.json.unreadable");
        touch(folder / "notes.txt");
        touch(folder / "legacy.PNG");
        const auto vues = store.listVues(ech);
        check(vues.size() == 4 && vues[0].filename() == "20261014-235959-999Z_ffffffffffffffff.tiff" &&
                  vues[1].filename() == "20261015-090000-500Z_aaaaaaaaaaaaaaaa.tiff" &&
                  vues[2].filename() == "20261015-090000-500Z_bbbbbbbbbbbbbbbb.tiff" &&
                  vues[3].filename() == "legacy.PNG",
              "listVues sorts by capture time and skips sidecars, set-aside files and non-images");
    }

    std::cout << "\n== Where Vues go by default ==\n";
    {
        check(defaultVueRoot() == defaultWorkstationConfigPath().parent_path() / "vues",
              "defaultVueRoot() is 'vues' beside the workstation config");
        WorkstationConfig config;
        check(config.vueRoot.empty(), "the workstation config leaves the Vue root to the default");
        config.vueRoot = dir / "D drive" / "Vues été";
        check(WorkstationConfig::fromJson(config.toJson()).vueRoot == config.vueRoot,
              "a chosen Vue root round-trips through the workstation config");
    }

    fs::remove_all(dir);
    return summary();
}
