#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// Which Échantillon a Vue is filed under, by Department, TP and Échantillon
// name. All three empty is the Unsorted bucket (ADR 0008): a workstation
// with no Department/TP/Échantillon yet still captures, never refuses to.
struct VueLocation {
    std::string department;
    std::string tp;
    std::string echantillon;

    static VueLocation unsorted() { return {}; }
    bool isUnsorted() const { return department.empty() && tp.empty() && echantillon.empty(); }
};

// Where Vues live on disk (ADR 0004: local first, every capture a new
// immutable file):
//
//   <root>/<Department>/<TP>/<Échantillon>/<UTC time>_<random>.tiff
//   <root>/Unsorted/<UTC time>_<random>.tiff
//
// e.g. 20261015-154353-123Z_9f2c4a7e1b3d5c60.tiff. The time sorts the files
// in capture order; the random part keeps two captures in the same
// millisecond apart. Each Vue's sidecar sits beside it (Vue::sidecarPath).
class VueStore {
public:
    explicit VueStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }

    // The folder holding the Vues of `location`; not created. Each name goes
    // through vueFolderName(), and a Department named "Unsorted" gets a
    // trailing '_' so it can't share the bucket's folder. Throws
    // std::invalid_argument when only some of department/TP/Échantillon are
    // given, or one has no usable characters.
    std::filesystem::path folder(const VueLocation& location) const;

    // A fresh path to pass to Vue::capture or CaptureStation::capture:
    // creates the folder, and never names a file that already exists.
    std::filesystem::path newVuePath(const VueLocation& location) const;

    // The Vues filed at `location`, oldest first. Empty when nothing was
    // captured there yet. Lists only the images, not sidecars or the files
    // a sidecar was set aside to.
    std::vector<std::filesystem::path> listVues(const VueLocation& location) const;

    // The Vue principale of `location` (CONTEXT.md): the one Vue flagged to
    // appear on its TP's Fiche (ADR 0016), recorded in vue-principale.json
    // in the Échantillon's folder. Empty when none is flagged — or when the
    // flag file can't be read or names no Vue there any more: losing the
    // flag costs no photo, and flagging again rewrites it.
    std::optional<std::filesystem::path> principale(const VueLocation& location) const;

    // Flags `vue`, a Vue filed at `location`, as its Vue principale. One
    // atomic write replaces the earlier flag: never two Vues flagged, never
    // none half-way. Throws std::invalid_argument when `vue` is not an
    // existing image in that location's folder.
    void setPrincipale(const VueLocation& location, const std::filesystem::path& vue) const;

    // Unflags the Vue principale of `location`, if any. The Vue stays.
    void clearPrincipale(const VueLocation& location) const;

    // Files `vue`, a Vue of this store, under the Échantillon `to` and
    // returns its new path: an Unsorted Vue once its TP exists (ADR 0008),
    // or one captured under the wrong Échantillon. Its files move together:
    // the image and every file named after it — the sidecar and the files a
    // sidecar was set aside to (<image>.json.unreadable, .mismatched, ...).
    // The file name, which starts with the capture time, is kept so the Vue
    // lists in capture order there; when another file already uses that
    // name in `to`, the Vue gets a fresh name from newVuePath() instead,
    // with its own extension. Nothing is ever overwritten (ADR 0004).
    // A Vue already filed under `to` stays where it is. A Vue principale is
    // unflagged in its old Échantillon; the flag does not follow it.
    //
    // The sidecar and set-aside files move first and the image last, so the
    // Vue never shows in `to` without its Treatments. When a move fails, the
    // files already moved are moved back and std::runtime_error says why —
    // and where each file is, should moving back fail too. Throws
    // std::invalid_argument, moving nothing, when `vue` is not an existing
    // Vue image in one of this store's folders, or `to` is Unsorted or
    // refused by folder().
    std::filesystem::path fileVue(const std::filesystem::path& vue, const VueLocation& to) const;

private:
    std::filesystem::path root_;
};

// A Department, TP or Échantillon name made safe as one folder name on
// Windows and Linux: characters Windows forbids (<>:"/\|?* and control
// characters) become '_', leading and trailing spaces and trailing dots are
// dropped, a reserved device name (CON, NUL, COM1...) gets a trailing '_',
// and it is cut to 64 bytes without splitting a UTF-8 character. Throws
// std::invalid_argument when nothing usable is left. "TP 1: Aciers" ->
// "TP 1_ Aciers".
std::string vueFolderName(const std::string& name);

// The default Vue root: "vues" beside the workstation config, in the
// per-user local app data folder — never roamed, never synced by OneDrive
// (a 4K capture is tens of MB). Throws like defaultWorkstationConfigPath().
std::filesystem::path defaultVueRoot();

} // namespace msapp
