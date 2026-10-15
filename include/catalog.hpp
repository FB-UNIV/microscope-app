#pragma once

#include "vue_store.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace msapp {

// The Departments, TPs and Échantillons a student picks from before
// capturing (see CONTEXT.md). Entered fresh, nothing is migrated from the
// legacy catalogs (ADR 0018). In Phase 1 a teacher writes catalog.json;
// in Phase 2 it becomes the local cache of what the API serves (ADR 0007).
// Like the legacy catalog, a TP carries the instructions shown to students;
// the legacy print codes and Échelle flag are gone (ADR 0010, 0016, 0017).
struct Echantillon {
    std::string name;  // e.g. "Ac1: XC10"
};

struct Tp {
    std::string name;          // e.g. "TP 1: Aciers"
    std::string instructions;  // shown to students next to the picker
    std::vector<Echantillon> echantillons;
};

struct Department {
    std::string name;  // e.g. "GMP"
    std::vector<Tp> tps;
};

struct Catalog {
    std::vector<Department> departments;

    bool empty() const { return departments.empty(); }

    // nullptr when there is no such Department or TP.
    const Department* department(const std::string& name) const;
    const Tp* tp(const std::string& department, const std::string& tp) const;

    // True when `location` names an Échantillon of this catalog. The
    // Unsorted bucket is always valid: capture never depends on a catalog
    // (ADR 0008).
    bool contains(const VueLocation& location) const;

    // Throws std::invalid_argument naming the problem when a name has no
    // usable characters for a folder, or two siblings would share a folder
    // under VueStore (the same name once made folder-safe, ignoring ASCII
    // case as Windows does) — their Vues would mix on disk.
    void validate() const;

    nlohmann::json toJson() const;
    // Accepts an Échantillon written as {"name": "..."} or as a bare string,
    // for teachers editing the file by hand. Missing "instructions" or
    // lists read as empty. Throws on anything malformed, and as validate().
    static Catalog fromJson(const nlohmann::json& j);
};

// catalog.json beside the workstation config. Throws like
// defaultWorkstationConfigPath().
std::filesystem::path defaultCatalogPath();

// What loadCatalog() read.
struct LoadedCatalog {
    Catalog catalog;
    // Why the file couldn't be read, naming it; empty when it loaded or
    // didn't exist yet.
    std::string error;
};

// A missing file is an empty catalog — captures go to Unsorted (ADR 0008).
// An unreadable one is empty too, with `error` saying why; the file is left
// as is and the next saveCatalog() keeps it aside.
LoadedCatalog loadCatalog(const std::filesystem::path& path);

// Writes `catalog` atomically, creating the folder; an existing file
// loadCatalog() can't read is first moved aside to <path>.unreadable
// (ADR 0004). Throws std::invalid_argument, writing nothing, for a catalog
// validate() refuses.
void saveCatalog(const Catalog& catalog, const std::filesystem::path& path);

} // namespace msapp
