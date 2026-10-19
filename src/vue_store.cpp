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
#include <utility>

namespace msapp {

namespace {

constexpr std::size_t kMaxFolderNameBytes = 64;
constexpr const char* kUnsortedFolder = "Unsorted";
constexpr const char* kCorbeilleFolder = "Corbeille";
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

// The file name the Vue principale flag of folder `dir` names; empty when
// there is no flag or it can't be read. Only a bare file name of an image
// counts, never a path out of the folder.
std::optional<std::filesystem::path> flaggedName(const std::filesystem::path& dir) {
    try {
        std::ifstream in(dir / kPrincipaleFile, std::ios::binary);
        if (!in) return std::nullopt;
        nlohmann::json flag;
        in >> flag;
        const std::filesystem::path name = std::filesystem::u8path(flag.at("vue").get<std::string>());
        if (name != name.filename() || !isVueImage(name)) return std::nullopt;
        return name;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool startsWith(const std::filesystem::path& name, const std::filesystem::path& prefix) {
    return name.native().compare(0, prefix.native().size(), prefix.native()) == 0;
}

// The files named after the Vue image `vue`, beside it: <image>.json, its
// sidecar, and <image>.json.unreadable, .mismatched..., the files a sidecar
// was set aside to.
std::vector<std::filesystem::path> filesNamedAfter(const std::filesystem::path& vue) {
    auto prefix = vue.filename();
    prefix += ".";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(vue.parent_path())) {
        if (entry.is_regular_file() && startsWith(entry.path().filename(), prefix)) files.push_back(entry.path());
    }
    return files;
}

// True when a Vue can be filed in `dir` as `name`: no file there has that
// name, or one named after it — a sidecar or set-aside file left without its
// image would become this Vue's.
bool nameIsFree(const std::filesystem::path& dir, const std::filesystem::path& name) {
    std::error_code error;
    if (std::filesystem::exists(dir / name, error)) return false;  // ignoring case, on Windows
    auto prefix = name;
    prefix += ".";
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::filesystem::path other = entry.path().filename();
        if (other == name || startsWith(other, prefix)) return false;
    }
    return true;
}

// Renames `from` to `to`, never over a file already at `to`: rename()
// itself would replace it.
std::error_code moveFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    if (std::filesystem::exists(to, error)) return std::make_error_code(std::errc::file_exists);
    std::filesystem::rename(from, to, error);
    return error;
}

// The root's own folders, which no Department may share: the Unsorted
// bucket and the Corbeille.
bool isReservedFolder(const std::string& name) {
    const std::string upper = toUpperAscii(name);
    return upper == toUpperAscii(kUnsortedFolder) || upper == toUpperAscii(kCorbeilleFolder);
}

// True when `vue` is an existing Vue image in one of `root`'s folders:
// Unsorted, or an Échantillon's three levels down — never the Corbeille.
bool isFiledVue(const std::filesystem::path& root, const std::filesystem::path& vue) {
    const std::filesystem::path from = vue.parent_path();
    const std::filesystem::path department = from.parent_path().parent_path();
    std::error_code error;
    const bool inStore = std::filesystem::equivalent(from, root / kUnsortedFolder, error) ||
                         (std::filesystem::equivalent(department.parent_path(), root, error) &&
                          !isReservedFolder(department.filename().u8string()));
    return isVueImage(vue) && std::filesystem::is_regular_file(vue, error) && inStore;
}

// Moves the Vue image `vue` to `target`, every file named after it renamed
// alike. The sidecar and set-aside files move first and the image last, so
// a listing never shows the image without its Treatments. When a move
// fails, the files already moved are moved back and std::runtime_error,
// starting with `context`, says why — and where each file is, should moving
// back fail too.
void moveVueFiles(const std::filesystem::path& vue, const std::filesystem::path& target, const std::string& context) {
    const std::filesystem::path from = vue.parent_path();
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    for (const auto& file : filesNamedAfter(vue)) {
        auto name = target.filename();  // <new image name> + what followed the old one
        name += file.filename().native().substr(vue.filename().native().size());
        moves.emplace_back(file, target.parent_path() / name);
    }
    moves.emplace_back(vue, target);

    for (std::size_t i = 0; i < moves.size(); ++i) {
        const std::error_code failure = moveFile(moves[i].first, moves[i].second);
        if (!failure) continue;
        const std::string why = context + ": could not move " + moves[i].first.string() + " to " +
                                moves[i].second.string() + " (" + failure.message() + ")";
        // Back where they were, so the Vue stays whole in its old folder.
        std::string stranded;
        while (i-- > 0) {
            if (moveFile(moves[i].second, moves[i].first)) stranded += "\n  " + moves[i].second.string();
        }
        if (stranded.empty()) throw std::runtime_error(why + "; the Vue stays in " + from.string());
        throw std::runtime_error(why + "; these files could not be moved back to " + from.string() +
                                 " either:" + stranded);
    }
}

// Unflags the Vue principale of folder `dir` when it names `vue`, which has
// just left it. A flag that can't be removed names a file gone from there,
// which principale() reads as none.
void unflagMoved(const std::filesystem::path& dir, const std::filesystem::path& vue) {
    std::error_code ignored;
    if (flaggedName(dir) == vue.filename()) std::filesystem::remove(dir / kPrincipaleFile, ignored);
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
    // Keeps a Department named "Unsorted" or "Corbeille" out of those folders.
    if (isReservedFolder(department)) department += '_';
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
    const auto name = flaggedName(dir);
    if (!name) return std::nullopt;
    const std::filesystem::path vue = dir / *name;
    std::error_code error;
    if (!std::filesystem::is_regular_file(vue, error)) return std::nullopt;
    return vue;
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

std::filesystem::path VueStore::fileVue(const std::filesystem::path& vue, const VueLocation& to) const {
    if (to.isUnsorted()) {
        throw std::invalid_argument("VueStore::fileVue: a Vue is filed under an Échantillon, not into Unsorted");
    }
    const std::filesystem::path dir = folder(to);
    const std::filesystem::path from = vue.parent_path();
    if (!isFiledVue(root_, vue)) {
        throw std::invalid_argument("VueStore::fileVue: " + vue.string() + " is not a Vue filed under " +
                                    root_.string());
    }
    std::error_code error;
    if (std::filesystem::equivalent(from, dir, error)) return vue;

    std::filesystem::create_directories(dir);
    std::filesystem::path target = dir / vue.filename();
    // Taken in practice only by a copy of this same Vue.
    for (int attempt = 0; !nameIsFree(dir, target.filename()); ++attempt) {
        if (attempt == 100) throw std::runtime_error("VueStore::fileVue: no free file name in " + dir.string());
        target = newVuePath(to).replace_extension(vue.extension());
    }
    moveVueFiles(vue, target, "VueStore::fileVue");
    unflagMoved(from, vue);
    return target;
}

std::filesystem::path VueStore::corbeille() const {
    return root_ / kCorbeilleFolder;
}

std::filesystem::path VueStore::deleteVue(const std::filesystem::path& vue) const {
    if (!isFiledVue(root_, vue)) {
        throw std::invalid_argument("VueStore::deleteVue: " + vue.string() + " is not a Vue filed under " +
                                    root_.string());
    }
    // The same folders under the Corbeille: Unsorted, or the last three
    // (Department, TP, Échantillon) of the Vue's own path.
    const std::filesystem::path from = vue.parent_path();
    std::filesystem::path dir = corbeille();
    std::error_code error;
    if (std::filesystem::equivalent(from, root_ / kUnsortedFolder, error)) {
        dir /= kUnsortedFolder;
    } else {
        dir = dir / from.parent_path().parent_path().filename() / from.parent_path().filename() / from.filename();
    }
    std::filesystem::create_directories(dir);

    std::filesystem::path target = dir / vue.filename();
    for (int n = 2; !nameIsFree(dir, target.filename()); ++n) {
        if (n == 100) throw std::runtime_error("VueStore::deleteVue: no free file name in " + dir.string());
        auto name = vue.stem();
        name += "-" + std::to_string(n);
        name += vue.extension();
        target = dir / name;
    }
    moveVueFiles(vue, target, "VueStore::deleteVue");
    unflagMoved(from, vue);
    return target;
}

} // namespace msapp
