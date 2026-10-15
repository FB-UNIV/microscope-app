#include "catalog.hpp"
#include "safe_write.hpp"
#include "workstation_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace msapp {

using detail::unusedAsidePath;
using detail::writeFileAtomically;

namespace {

// Two names with the same key would share a folder under VueStore.
std::string folderKey(const std::string& name) {
    std::string key = vueFolderName(name);
    for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return key;
}

// Throws unless every item of `items` gets its own folder. `kind` is
// "Department", "TP" or "Échantillon"; `where` says whose children they are.
template <typename Item>
void requireOwnFolders(const std::vector<Item>& items, const std::string& kind, const std::string& where) {
    std::map<std::string, std::string> seen;  // folder key -> first name with it
    for (const auto& item : items) {
        std::string key;
        try {
            key = folderKey(item.name);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("Catalog: " + kind + " '" + item.name + "'" + where +
                                        " has no characters usable in a folder name");
        }
        const auto [it, inserted] = seen.emplace(key, item.name);
        if (!inserted) {
            throw std::invalid_argument("Catalog: " + kind + "s '" + it->second + "' and '" + item.name + "'" + where +
                                        " would share a folder; rename one");
        }
    }
}

// The array under `key`; empty when missing.
const nlohmann::json& arrayAt(const nlohmann::json& j, const char* key) {
    static const nlohmann::json none = nlohmann::json::array();
    if (!j.contains(key)) return none;
    const nlohmann::json& value = j.at(key);
    if (!value.is_array()) {
        throw std::invalid_argument(std::string("Catalog::fromJson: \"") + key + "\" must be an array");
    }
    return value;
}

// Throws whatever stops `path` loading as a Catalog.
Catalog readCatalogFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open the file");
    }
    nlohmann::json j;
    in >> j;
    return Catalog::fromJson(j);
}

} // namespace

const Department* Catalog::department(const std::string& name) const {
    auto it = std::find_if(departments.begin(), departments.end(),
                           [&](const Department& d) { return d.name == name; });
    return it == departments.end() ? nullptr : &*it;
}

const Tp* Catalog::tp(const std::string& departmentName, const std::string& tpName) const {
    const Department* d = department(departmentName);
    if (d == nullptr) return nullptr;
    auto it = std::find_if(d->tps.begin(), d->tps.end(), [&](const Tp& t) { return t.name == tpName; });
    return it == d->tps.end() ? nullptr : &*it;
}

bool Catalog::contains(const VueLocation& location) const {
    if (location.isUnsorted()) return true;
    const Tp* t = tp(location.department, location.tp);
    return t != nullptr && std::any_of(t->echantillons.begin(), t->echantillons.end(),
                                       [&](const Echantillon& e) { return e.name == location.echantillon; });
}

void Catalog::validate() const {
    requireOwnFolders(departments, "Department", "");
    for (const auto& d : departments) {
        requireOwnFolders(d.tps, "TP", " in Department '" + d.name + "'");
        for (const auto& t : d.tps) {
            requireOwnFolders(t.echantillons, "Échantillon", " in TP '" + t.name + "'");
        }
    }
}

nlohmann::json Catalog::toJson() const {
    nlohmann::json departmentsJson = nlohmann::json::array();
    for (const auto& d : departments) {
        nlohmann::json tpsJson = nlohmann::json::array();
        for (const auto& t : d.tps) {
            nlohmann::json echantillonsJson = nlohmann::json::array();
            for (const auto& e : t.echantillons) echantillonsJson.push_back({{"name", e.name}});
            tpsJson.push_back({{"name", t.name}, {"instructions", t.instructions}, {"echantillons", echantillonsJson}});
        }
        departmentsJson.push_back({{"name", d.name}, {"tps", tpsJson}});
    }
    return {{"departments", departmentsJson}};
}

Catalog Catalog::fromJson(const nlohmann::json& j) {
    // Checked up front: a stray array would otherwise read as an empty
    // catalog and silently send every capture to Unsorted.
    if (!j.is_object()) {
        throw std::invalid_argument("Catalog::fromJson: expected a JSON object");
    }
    Catalog catalog;
    for (const auto& departmentJson : arrayAt(j, "departments")) {
        Department d;
        d.name = departmentJson.at("name").get<std::string>();
        for (const auto& tpJson : arrayAt(departmentJson, "tps")) {
            Tp t;
            t.name = tpJson.at("name").get<std::string>();
            t.instructions = tpJson.value("instructions", std::string());
            for (const auto& e : arrayAt(tpJson, "echantillons")) {
                t.echantillons.push_back({e.is_string() ? e.get<std::string>() : e.at("name").get<std::string>()});
            }
            d.tps.push_back(std::move(t));
        }
        catalog.departments.push_back(std::move(d));
    }
    catalog.validate();
    return catalog;
}

std::filesystem::path defaultCatalogPath() {
    return defaultWorkstationConfigPath().parent_path() / "catalog.json";
}

LoadedCatalog loadCatalog(const std::filesystem::path& path) {
    LoadedCatalog loaded;
    try {
        if (!std::filesystem::exists(path)) {
            return loaded;  // no catalog yet: captures go to Unsorted
        }
        loaded.catalog = readCatalogFile(path);
    } catch (const std::exception& e) {
        loaded.error = path.string() + ": " + e.what();
    }
    return loaded;
}

void saveCatalog(const Catalog& catalog, const std::filesystem::path& path) {
    catalog.validate();
    // Never overwrite a file load could not read (ADR 0004).
    if (std::filesystem::exists(path)) {
        try {
            readCatalogFile(path);
        } catch (const std::exception&) {
            std::filesystem::rename(path, unusedAsidePath(path, ".unreadable"));
        }
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    writeFileAtomically(path, catalog.toJson().dump(2), "saveCatalog");
}

} // namespace msapp
