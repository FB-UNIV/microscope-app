// User journey:
//   As a student, every photo I capture lands in my Échantillon's folder
//   under a name nobody else's capture can take, even when two of us click
//   in the same second; before any TP exists on this workstation, my photos
//   still save, into Unsorted (ADR 0008), and once the TP exists I file
//   each one into its Échantillon with its Treatments. A blurred photo I
//   delete goes to a Corbeille it can still be recovered from. As a
//   teacher, I can list the Vues of one Échantillon in the order they were
//   taken.

#include "levels_treatment.hpp"
#include "vue.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"
#include "test_support.hpp"

#include <opencv2/core.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

fs::path withSuffix(fs::path path, const std::string& suffix) {
    path += suffix;
    return path;
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool lists(const std::vector<fs::path>& vues, const fs::path& vue) {
    return std::find(vues.begin(), vues.end(), vue) != vues.end();
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
        check(store.folder({"Corbeille", "TP", "E"}) == dir / "vues" / "Corbeille_" / "TP" / "E" &&
                  store.corbeille() == dir / "vues" / "Corbeille",
              "nor can one named Corbeille share the deleted Vues' folder");
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

    std::cout << "\n== Vue principale: the one Vue on the Fiche (ADR 0016) ==\n";
    {
        const VueLocation ech{"GMP", "TP 4", "Principale"};
        const cv::Mat frame(8, 8, CV_8UC3, cv::Scalar(1, 2, 3));
        const Vue first = Vue::capture(frame, store.newVuePath(ech));
        const Vue second = Vue::capture(frame, store.newVuePath(ech));
        check(!store.principale(ech), "no Vue principale until one is flagged");

        store.setPrincipale(ech, first.imagePath());
        check(store.principale(ech) == first.imagePath(), "the flagged Vue is the Vue principale");
        store.setPrincipale(ech, second.imagePath());
        check(store.principale(ech) == second.imagePath(), "flagging another one replaces it: never two");
        check(store.listVues(ech).size() == 2, "the flag file is not listed as a Vue");

        const Vue elsewhere = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        check(!throwsWith<std::invalid_argument>([&] { store.setPrincipale(ech, elsewhere.imagePath()); }).empty() &&
                  store.principale(ech) == second.imagePath(),
              "a Vue of another Échantillon can't be flagged here, and the flag stays");
        check(!throwsWith<std::invalid_argument>([&] { store.setPrincipale(ech, store.folder(ech) / "missing.tiff"); })
                   .empty(),
              "a file that doesn't exist can't be flagged");

        std::ofstream(store.folder(ech) / "vue-principale.json", std::ios::binary | std::ios::trunc)
            << "{\"vue\": \"../../../Unsorted/" << elsewhere.imagePath().filename().string() << "\"}";
        check(!store.principale(ech), "a flag pointing outside the Échantillon's folder is ignored");
        std::ofstream(store.folder(ech) / "vue-principale.json", std::ios::binary | std::ios::trunc) << "{oops";
        check(!store.principale(ech), "an unreadable flag means none flagged, not an error");

        store.setPrincipale(ech, first.imagePath());
        store.clearPrincipale(ech);
        check(!store.principale(ech) && fs::exists(first.imagePath()), "clearing unflags it; the Vue stays");
        store.clearPrincipale(ech);
        check(!store.principale(ech), "clearing when none is flagged is fine");
    }

    std::cout << "\n== fileVue: an Unsorted Vue filed into its Échantillon (ADR 0008) ==\n";
    const cv::Mat frame(48, 64, CV_8UC3, cv::Scalar(30, 120, 200));
    const VueLocation tp5{"GMP", "TP 5", "Ac1: XC10"};
    {
        Vue vue = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()), CaptureInfo{},
                               Calibration{"x10", 2.5, 64, 48});
        vue.addTreatment(std::make_unique<LevelsTreatment>(20, 230));
        const cv::Mat rendered = vue.render();
        const fs::path old = vue.imagePath();
        touch(withSuffix(vue.sidecarPath(), ".unreadable"));
        touch(withSuffix(vue.sidecarPath(), ".mismatched.2"));

        const fs::path filed = store.fileVue(old, tp5);
        check(filed == store.folder(tp5) / old.filename(),
              "the Vue keeps its file name — its capture time, so its place in the list");
        check(!fs::exists(old) && !fs::exists(withSuffix(old, ".json")) &&
                  !fs::exists(withSuffix(old, ".json.unreadable")) && !fs::exists(withSuffix(old, ".json.mismatched.2")),
              "nothing of it is left in Unsorted");
        check(fs::exists(filed) && fs::exists(withSuffix(filed, ".json")) &&
                  fs::exists(withSuffix(filed, ".json.unreadable")) && fs::exists(withSuffix(filed, ".json.mismatched.2")),
              "the image, its sidecar and its set-aside files moved together");
        check(!lists(store.listVues(VueLocation::unsorted()), old) &&
                  store.listVues(tp5) == std::vector<fs::path>{filed},
              "it lists under its Échantillon, no longer in Unsorted");

        const Vue reloaded = Vue::load(filed);
        check(!reloaded.sidecarError() && reloaded.treatmentCount() == vue.treatmentCount() &&
                  reloaded.calibration() && nearlyEqual(reloaded.calibration()->pixelsPerMicron, 2.5) &&
                  cv::norm(reloaded.render(), rendered, cv::NORM_INF) == 0,
              "the filed Vue loads with its Treatments and Calibration, rendering as before");

        check(store.fileVue(filed, tp5) == filed && fs::exists(filed), "filing a Vue where it already is changes nothing");
    }
    {
        // Another file already has the name there, e.g. a copy of this Vue.
        const Vue png = Vue::capture(frame, store.folder(VueLocation::unsorted()) / "imported.png");
        const fs::path taken = store.folder(tp5) / "imported.png";
        std::ofstream(taken, std::ios::binary) << "earlier";
        const fs::path filed = store.fileVue(png.imagePath(), tp5);
        check(filed != taken && filed.parent_path() == store.folder(tp5) && filed.extension() == ".png" &&
                  fs::exists(filed) && readAll(taken) == "earlier",
              "a name already taken is never overwritten: a fresh one, keeping the image's extension");

        const Vue orphaned = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        const fs::path leftover = store.folder(tp5) / (orphaned.imagePath().filename().string() + ".json");
        std::ofstream(leftover, std::ios::binary) << "someone else's";
        const fs::path refiled = store.fileVue(orphaned.imagePath(), tp5);
        check(refiled.filename() != orphaned.imagePath().filename() && readAll(leftover) == "someone else's",
              "nor is a sidecar left there without its image taken over");
    }
    {
        const Vue vue = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        const fs::path unsortedFolder = store.folder(VueLocation::unsorted());
        touch(unsortedFolder / "notes.txt");
        const auto refused = [&](const fs::path& path, const VueLocation& to) {
            return !throwsWith<std::invalid_argument>([&] { store.fileVue(path, to); }).empty();
        };
        check(refused(unsortedFolder / "missing.tiff", tp5), "a file that doesn't exist is refused");
        check(refused(unsortedFolder / "notes.txt", tp5) && refused(vue.sidecarPath(), tp5) &&
                  fs::exists(unsortedFolder / "notes.txt"),
              "a file that isn't a Vue image (a note, a sidecar) is refused");
        check(refused(vue.imagePath(), VueLocation::unsorted()) && refused(vue.imagePath(), {"GMP", "", "A"}),
              "filing into Unsorted, or under half a location, is refused");
        fs::create_directories(dir / "elsewhere");
        const Vue outside = Vue::capture(frame, dir / "elsewhere" / "photo.tiff");
        check(refused(outside.imagePath(), tp5) && fs::exists(outside.imagePath()),
              "a photo outside the store is refused, not moved in");
        check(fs::exists(vue.imagePath()) && lists(store.listVues(VueLocation::unsorted()), vue.imagePath()),
              "a refused Vue stays where it was");
    }

    std::cout << "\n== fileVue: a misfiled Vue principale ==\n";
    {
        const VueLocation wrong{"GMP", "TP 6", "Ac1"};
        const VueLocation right{"GMP", "TP 6", "Ac2"};
        const Vue first = Vue::capture(frame, store.newVuePath(wrong));
        const Vue second = Vue::capture(frame, store.newVuePath(wrong));
        store.setPrincipale(wrong, first.imagePath());

        store.fileVue(second.imagePath(), right);
        check(store.principale(wrong) == first.imagePath(), "filing another Vue away leaves the flag alone");
        check(store.fileVue(first.imagePath(), wrong) == first.imagePath() && store.principale(wrong) == first.imagePath(),
              "nor does filing the Vue principale where it already is");

        const fs::path moved = store.fileVue(first.imagePath(), right);
        check(fs::exists(moved) && !store.principale(wrong) && !fs::exists(store.folder(wrong) / "vue-principale.json"),
              "the Vue principale filed elsewhere is unflagged in its old Échantillon");
        check(!store.principale(right), "and the flag does not follow it: the teacher picks again there");
    }

    std::cout << "\n== fileVue: a move that fails half-way ==\n";
    {
        Vue vue = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        vue.addTreatment(std::make_unique<LevelsTreatment>(10, 200));
        const fs::path image = vue.imagePath();
        const fs::path sidecar = vue.sidecarPath();
        const fs::path target = store.folder(tp5) / image.filename();
#ifdef _WIN32
        // A photo open in another program can't be moved on Windows: the
        // sidecar moves first, then the image fails.
        std::ifstream held(image, std::ios::binary);
        const std::string error = throwsWith<std::runtime_error>([&] { store.fileVue(image, tp5); });
        check(!error.empty() && error.find("stays in") != std::string::npos, "the failed move says so");
        check(fs::exists(image) && readAll(sidecar).find("levels") != std::string::npos && !fs::exists(target) &&
                  !fs::exists(withSuffix(target, ".json")),
              "the sidecar already moved comes back: the Vue stays whole in Unsorted");
        held.close();
        check(store.fileVue(image, tp5) == target && Vue::load(target).treatmentCount() == 1,
              "once the photo is closed, filing it works");
#else
        // A folder it can't be moved into: nothing moves.
        const VueLocation locked{"GMP", "TP 7", "Locked"};
        const fs::path lockedFolder = store.folder(locked);
        fs::create_directories(lockedFolder);
        fs::permissions(lockedFolder, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
        if (std::ofstream(lockedFolder / "probe")) {
            std::cout << "  [SKIP] running as root: a read-only folder can't refuse a move\n";
        } else {
            const std::string error = throwsWith<std::runtime_error>([&] { store.fileVue(image, locked); });
            check(!error.empty() && error.find("stays in") != std::string::npos, "the failed move says so");
            check(fs::exists(image) && fs::exists(sidecar) && store.listVues(locked).empty(),
                  "the Vue stays whole in Unsorted");
        }
        fs::permissions(lockedFolder, fs::perms::owner_all, fs::perm_options::replace);
        check(store.fileVue(image, tp5) == target && Vue::load(target).treatmentCount() == 1,
              "filed into a folder it can be moved to, it works");
#endif
    }

    std::cout << "\n== deleteVue: to the Corbeille, never erased ==\n";
    {
        const VueLocation ech{"GMP", "TP 8", "Ac1: XC10"};
        Vue vue = Vue::capture(frame, store.newVuePath(ech), CaptureInfo{}, Calibration{"x10", 2.5, 64, 48});
        vue.addTreatment(std::make_unique<LevelsTreatment>(20, 230));
        const Vue kept = Vue::capture(frame, store.newVuePath(ech), CaptureInfo{});  // with a sidecar
        const fs::path image = vue.imagePath();
        touch(withSuffix(vue.sidecarPath(), ".unreadable"));
        store.setPrincipale(ech, image);

        const fs::path deleted = store.deleteVue(image);
        check(deleted == store.corbeille() / "GMP" / "TP 8" / "Ac1_ XC10" / image.filename(),
              "a deleted Vue goes under the same Department / TP / Échantillon folders in the Corbeille");
        check(!fs::exists(image) && !fs::exists(withSuffix(image, ".json")) &&
                  !fs::exists(withSuffix(image, ".json.unreadable")),
              "nothing of it is left in its Échantillon");
        check(fs::exists(withSuffix(deleted, ".json")) && fs::exists(withSuffix(deleted, ".json.unreadable")),
              "its sidecar and set-aside files moved with it");
        check(store.listVues(ech) == std::vector<fs::path>{kept.imagePath()}, "it no longer lists; the others do");
        check(!store.principale(ech) && !fs::exists(store.folder(ech) / "vue-principale.json"),
              "a deleted Vue principale is unflagged");
        const Vue recovered = Vue::load(deleted);
        check(!recovered.sidecarError() && recovered.treatmentCount() == 1 && recovered.calibration(),
              "in the Corbeille it still loads with its Treatments and Calibration: nothing is erased");

        store.setPrincipale(ech, kept.imagePath());
        const Vue other = Vue::capture(frame, store.newVuePath(ech));
        store.deleteVue(other.imagePath());
        check(store.principale(ech) == kept.imagePath(), "deleting another Vue leaves the flag alone");

        // Put back by hand, then deleted again: the first copy stays.
        fs::rename(deleted, image);
        fs::rename(withSuffix(deleted, ".json"), withSuffix(image, ".json"));
        check(lists(store.listVues(ech), image), "moving its files back by hand restores it");
        std::ofstream(withSuffix(deleted, ".json.unreadable"), std::ios::binary | std::ios::app) << "first";
        const fs::path again = store.deleteVue(image);
        check(again == deleted.parent_path() / (image.stem().string() + "-2" + image.extension().string()) &&
                  fs::exists(withSuffix(again, ".json")) && readAll(withSuffix(deleted, ".json.unreadable")) == "xfirst",
              "deleted again, it gets a '-2' name; nothing already in the Corbeille is overwritten");

        const Vue unsorted = Vue::capture(frame, store.newVuePath(VueLocation::unsorted()));
        const fs::path unsortedDeleted = store.deleteVue(unsorted.imagePath());
        check(unsortedDeleted == store.corbeille() / "Unsorted" / unsorted.imagePath().filename() &&
                  !lists(store.listVues(VueLocation::unsorted()), unsorted.imagePath()),
              "an Unsorted Vue goes to Corbeille/Unsorted");

        const auto refused = [&](const fs::path& path) {
            return !throwsWith<std::invalid_argument>([&] { store.deleteVue(path); }).empty();
        };
        check(refused(again) && fs::exists(again), "a Vue already in the Corbeille is refused, and stays");
        check(refused(store.folder(ech) / "missing.tiff") && refused(kept.sidecarPath()) &&
                  fs::exists(kept.sidecarPath()),
              "a missing file, or a sidecar, is refused");
        const Vue outside = Vue::capture(frame, dir / "elsewhere" / "photo2.tiff");
        check(refused(outside.imagePath()) && fs::exists(outside.imagePath()), "a photo outside the store is refused");
        check(!throwsWith<std::invalid_argument>([&] { store.fileVue(again, ech); }).empty(),
              "and a Vue in the Corbeille can't be filed back into an Échantillon either");
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
