// User journey:
//   As a teacher, I write this year's Departments, TPs and Échantillons in
//   catalog.json, with the instructions students see for each TP; the app
//   tells me if two names would end up in the same folder. As a student, I
//   pick "TP 1: Aciers" / "Ac1: XC10" before capturing — and with no
//   catalog at all, I can still capture, into Unsorted (ADR 0008).

#include "catalog.hpp"
#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace msapp;
using namespace msapp::test;
namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

Catalog aciers() {
    Catalog catalog;
    catalog.departments = {
        {"GMP",
         {{"TP 1: Aciers", "Estimez la teneur en carbone.", {{"Ac1: XC10"}, {"Ac2: XC38"}}},
          {"TP 2: Fontes", "", {{"F1"}}}}},
        {"CHIMIE", {}},
    };
    return catalog;
}

std::string refusal(const Catalog& catalog) {
    return throwsWith<std::invalid_argument>([&] { catalog.validate(); });
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "msapp_catalog_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    std::cout << "== Looking up what a student picks ==\n";
    {
        const Catalog catalog = aciers();
        check(catalog.department("GMP") != nullptr && catalog.department("ISMANS") == nullptr,
              "Departments are found by name");
        const Tp* tp = catalog.tp("GMP", "TP 1: Aciers");
        check(tp != nullptr && tp->instructions == "Estimez la teneur en carbone." && tp->echantillons.size() == 2,
              "a TP carries its instructions and Échantillons");
        check(catalog.tp("GMP", "TP 9") == nullptr && catalog.tp("ISMANS", "TP 1: Aciers") == nullptr,
              "an unknown TP or Department is not found");
        check(catalog.contains({"GMP", "TP 1: Aciers", "Ac2: XC38"}), "a picked Échantillon is in the catalog");
        check(!catalog.contains({"GMP", "TP 1: Aciers", "F1"}) && !catalog.contains({"GMP", "TP 2: Fontes", "Ac1: XC10"}),
              "an Échantillon of another TP is not");
        check(catalog.contains(VueLocation::unsorted()) && Catalog{}.contains(VueLocation::unsorted()),
              "Unsorted is always a valid place to capture, even with no catalog");
    }

    std::cout << "\n== validate: no two names share a folder ==\n";
    {
        check(refusal(aciers()).empty(), "a catalog with distinct names is accepted");

        Catalog sameFolder = aciers();
        sameFolder.departments[0].tps[0].echantillons.push_back({"Ac1/ XC10"});
        const std::string why = refusal(sameFolder);
        check(why.find("Ac1: XC10") != std::string::npos && why.find("Ac1/ XC10") != std::string::npos &&
                  why.find("TP 1: Aciers") != std::string::npos,
              "two Échantillons that become the same folder are refused, naming both and their TP");

        Catalog caseOnly = aciers();
        caseOnly.departments.push_back({"gmp", {}});
        check(!refusal(caseOnly).empty(), "Department names differing only in case are refused (Windows folders)");

        Catalog sameTp = aciers();
        sameTp.departments[0].tps.push_back({"TP 1: Aciers.", "", {}});
        check(!refusal(sameTp).empty(), "a TP name differing only by a trailing dot is refused");

        Catalog blank = aciers();
        blank.departments[1].tps.push_back({"  ", "", {}});
        check(refusal(blank).find("no characters usable") != std::string::npos, "a blank name is refused");

        Catalog elsewhere = aciers();
        elsewhere.departments[1].tps.push_back({"TP 1: Aciers", "", {{"Ac1: XC10"}}});
        check(refusal(elsewhere).empty(), "the same names in another Department are fine: other folders");
    }

    std::cout << "\n== JSON a teacher writes ==\n";
    {
        const Catalog back = Catalog::fromJson(aciers().toJson());
        const Tp* tp = back.tp("GMP", "TP 1: Aciers");
        check(back.departments.size() == 2 && tp != nullptr && tp->instructions == "Estimez la teneur en carbone." &&
                  tp->echantillons.size() == 2 && tp->echantillons[1].name == "Ac2: XC38",
              "the catalog round-trips through JSON");

        const Catalog shorthand = Catalog::fromJson(nlohmann::json::parse(R"({"departments": [
            {"name": "GMP", "tps": [{"name": "TP 1", "echantillons": ["A", {"name": "B"}]}]}]})"));
        check(shorthand.tp("GMP", "TP 1") && shorthand.tp("GMP", "TP 1")->echantillons.size() == 2 &&
                  shorthand.tp("GMP", "TP 1")->instructions.empty(),
              "Échantillons may be bare strings, and instructions may be left out");
        check(Catalog::fromJson(nlohmann::json::object()).empty(), "an empty object is an empty catalog");
        check(!throwsWith<std::invalid_argument>([] { Catalog::fromJson(nlohmann::json::array()); }).empty(),
              "a JSON array is refused instead of read as an empty catalog");
        check(!throwsWith<std::invalid_argument>([] {
                  Catalog::fromJson(nlohmann::json::parse(R"({"departments": {"name": "GMP"}})"));
              }).empty(),
              "\"departments\" must be a list");

        const LoadedCatalog example = loadCatalog(fs::path(MSAPP_SOURCE_DIR) / "docs" / "catalog-example.json");
        check(example.error.empty() && example.catalog.contains({"GMP", "TP 1: Aciers", "Ac1: XC10"}),
              "docs/catalog-example.json loads");
    }

    std::cout << "\n== Disk ==\n";
    {
        const fs::path path = dir / "app data" / "catalog.json";
        const LoadedCatalog none = loadCatalog(path);
        check(none.error.empty() && none.catalog.empty(), "no file yet: an empty catalog, not an error");

        saveCatalog(aciers(), path);
        const LoadedCatalog loaded = loadCatalog(path);
        check(loaded.error.empty() && loaded.catalog.contains({"GMP", "TP 2: Fontes", "F1"}),
              "a saved catalog loads back");

        writeFile(path, "{\"departments\": [");
        const LoadedCatalog damaged = loadCatalog(path);
        check(!damaged.error.empty() && damaged.catalog.empty() && damaged.error.find(path.string()) != std::string::npos,
              "a damaged file loads as an empty catalog with an error naming it");
        saveCatalog(aciers(), path);
        check(readFile(path.string() + ".unreadable") == "{\"departments\": [",
              "saving keeps the damaged file aside instead of overwriting it");

        Catalog invalid = aciers();
        invalid.departments.push_back({"GMP", {}});
        const std::string before = readFile(path);
        check(!throwsWith<std::invalid_argument>([&] { saveCatalog(invalid, path); }).empty() && readFile(path) == before,
              "an invalid catalog is not saved, and the file is kept as it was");
        check(defaultCatalogPath().filename() == "catalog.json", "catalog.json sits beside the workstation config");
    }

    fs::remove_all(dir);
    return summary();
}
