#include "vue_store.hpp"
#include "safe_write.hpp"
#include "workstation_config.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

namespace msapp {

namespace {

constexpr std::size_t kMaxFolderNameBytes = 64;
constexpr const char* kUnsortedFolder = "Unsorted";
constexpr const char* kVueExtension = ".tiff";
constexpr const char* kPrincipaleFile = "vue-principale.json";

std::string toUpperAscii(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// Windows reserves these device names, with any extension ("CON.txt" too).
bool isReservedDeviceName(const std::string& name) {
    const std::string stem = toUpperAscii(name.substr(0, name.find('.')));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
    return stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
           stem[3] >= '1' && stem[3] <= '9';
}

// Windows drops trailing spaces and dots from a folder name, so two names
// differing only there would share a folder.
void trimFolderName(std::string& name) {
    const auto first = name.find_first_not_of(' ');
    name.erase(0, first == std::string::npos ? name.size() : first);
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
}

// UTC, so the order of names is the order of captures even across a
// daylight-saving change: 20261015-154353-123Z.
std::string utcTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t seconds = system_clock::to_time_t(now);
    const auto millis = static_cast<int>(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char date[32];
    std::strftime(date, sizeof date, "%Y%m%d-%H%M%S", &utc);
    char text[48];
    std::snprintf(text, sizeof text, "%s-%03dZ", date, millis);
    return text;
}

// 64 random bits as 16 hex digits.
std::string randomSuffix() {
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(engine()));
    return text;
}

bool isVueImage(const std::filesystem::path& path) {
    const std::string extension = toUpperAscii(path.extension().string());
    return extension == ".TIFF" || extension == ".TIF" || extension == ".PNG" || extension == ".JPG" ||
           extension == ".JPEG" || extension == ".BMP";
}

} // namespace

std::string vueFolderName(const std::string& name) {
    std::string folder;
    folder.reserve(name.size());
    for (char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        const bool forbidden = byte < 0x20 || byte == 0x7f || std::strchr("<>:\"/\\|?*", c) != nullptr;
        folder += forbidden ? '_' : c;
    }
    trimFolderName(folder);
    if (folder.size() > kMaxFolderNameBytes) {
        // Back up to the start of a UTF-8 character (continuation bytes are
        // 10xxxxxx), so the cut never leaves half of one.
        std::size_t cut = kMaxFolderNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(folder[cut]) & 0xC0) == 0x80) --cut;
        folder.resize(cut);
        trimFolderName(folder);
    }
    if (folder.empty()) {
        throw std::invalid_argument("vueFolderName: '" + name + "' has no characters usable in a folder name");
    }
    if (isReservedDeviceName(folder)) folder += '_';
    return folder;
}

std::filesystem::path defaultVueRoot() {
    return defaultWorkstationConfigPath().parent_path() / "vues";
}

VueStore::VueStore(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path VueStore::folder(const VueLocation& location) const {
    if (location.isUnsorted()) return root_ / kUnsortedFolder;
    if (location.department.empty() || location.tp.empty() || location.echantillon.empty()) {
        throw std::invalid_argument("VueStore::folder: a Vue is filed under a Department, a TP and an Échantillon, "
                                    "or under none of them (Unsorted); got Department '" + location.department +
                                    "', TP '" + location.tp + "', Échantillon '" + location.echantillon + "'");
    }
    std::string department = vueFolderName(location.department);
    // Keeps a Department named "Unsorted" out of the Unsorted bucket.
    if (toUpperAscii(department) == toUpperAscii(kUnsortedFolder)) department += '_';
    return root_ / std::filesystem::u8path(department) / std::filesystem::u8path(vueFolderName(location.tp)) /
           std::filesystem::u8path(vueFolderName(location.echantillon));
}

std::filesystem::path VueStore::newVuePath(const VueLocation& location) const {
    const std::filesystem::path dir = folder(location);
    std::filesystem::create_directories(dir);
    // One try is all it ever takes: 64 random bits within one millisecond.
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = dir / (utcTimestamp() + "_" + randomSuffix() + kVueExtension);
        if (!std::filesystem::exists(candidate)) return candidate;
    }
    throw std::runtime_error("VueStore::newVuePath: no free file name in " + dir.string());
}

std::vector<std::filesystem::path> VueStore::listVues(const VueLocation& location) const {
    std::vector<std::filesystem::path> vues;
    const std::filesystem::path dir = folder(location);
    std::error_code error;
    if (!std::filesystem::is_directory(dir, error)) return vues;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && isVueImage(entry.path())) vues.push_back(entry.path());
    }
    // File names start with the UTC capture time.
    std::sort(vues.begin(), vues.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename().native() < b.filename().native();
    });
    return vues;
}

std::optional<std::filesystem::path> VueStore::principale(const VueLocation& location) const {
    const std::filesystem::path dir = folder(location);
    try {
        std::ifstream in(dir / kPrincipaleFile, std::ios::binary);
        if (!in) return std::nullopt;
        nlohmann::json flag;
        in >> flag;
        const std::filesystem::path name = std::filesystem::u8path(flag.at("vue").get<std::string>());
        // A bare file name in this folder, never a path out of it.
        if (name != name.filename() || !isVueImage(name)) return std::nullopt;
        const std::filesystem::path vue = dir / name;
        if (!std::filesystem::is_regular_file(vue)) return std::nullopt;
        return vue;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void VueStore::setPrincipale(const VueLocation& location, const std::filesystem::path& vue) const {
    const std::filesystem::path dir = folder(location);
    std::error_code error;
    const bool filedHere = isVueImage(vue) && std::filesystem::is_regular_file(vue, error) &&
                           std::filesystem::equivalent(vue.parent_path(), dir, error);
    if (!filedHere) {
        throw std::invalid_argument("VueStore::setPrincipale: " + vue.string() + " is not a Vue filed in " +
                                    dir.string());
    }
    detail::writeFileAtomically(dir / kPrincipaleFile, nlohmann::json{{"vue", vue.filename().u8string()}}.dump(2),
                                "VueStore::setPrincipale");
}

void VueStore::clearPrincipale(const VueLocation& location) const {
    std::error_code ignored;  // none flagged is fine
    std::filesystem::remove(folder(location) / kPrincipaleFile, ignored);
}

} // namespace msapp
