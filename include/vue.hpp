#pragma once

#include "treatment.hpp"
#include "white_balance.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace msapp {

// How a Vue was captured (ADR 0014): the camera settings CaptureStation
// applied and the White reference gains already baked into the original,
// so a photo's colour can be explained or reproduced later. Stored under
// "capture" in the sidecar.
struct CaptureInfo {
    std::string cameraBackend;
    std::optional<double> exposureMs;
    std::optional<double> whiteBalanceKelvin;
    WhiteBalance whiteReference;

    nlohmann::json toJson() const;
    static CaptureInfo fromJson(const nlohmann::json& j);
};

// Vue (see CONTEXT.md): one saved photo + its Treatments.
//
// Implements ADR 0003 (plain original + JSON sidecar, not baked-in) and
// ADR 0002 (treatments as an ordered, non-destructive pipeline): the image
// file on disk is written once, at capture, and never rewritten. Every
// Treatment applied afterward is recorded in a same-path ".json" sidecar
// and re-applied on top of the untouched original whenever the Vue is
// loaded or rendered — never baked into the stored file.
class Vue {
public:
    // Captures a new Vue: writes `original` unmodified to `imagePath` and
    // returns a Vue with an empty treatment stack. Fails (throws) if
    // `imagePath` already exists — Vues are immutable-by-path (see the
    // race/overwrite discussion behind ADR 0004); callers must generate a
    // fresh unique path per capture, never reuse one.
    // With captureInfo, the sidecar is written immediately so the capture
    // settings exist on disk even before any Treatment is added.
    static Vue capture(const cv::Mat& original, const std::filesystem::path& imagePath,
                       std::optional<CaptureInfo> captureInfo = std::nullopt);

    // Loads an existing Vue: reads the original image, plus its sidecar
    // (imagePath + ".json") if one exists — a Vue with no treatments yet
    // simply has no sidecar file, which is a valid state, not an error.
    // A sidecar that exists but cannot be read (corrupt JSON, unknown
    // treatment type, bad params) does not stop the photo loading (ADR 0003):
    // the Vue comes back with no treatments and no capture info, and
    // sidecarError() says why. Only an unreadable image throws.
    static Vue load(const std::filesystem::path& imagePath);

    // Appends a treatment to the pipeline and persists the updated sidecar
    // immediately (no separate "save" step — matches the sidecar being the
    // single source of truth for what's applied). The sidecar is replaced
    // atomically; if load() found it unreadable, it is first kept aside as
    // sidecarPath() + ".unreadable" rather than overwritten (ADR 0004).
    void addTreatment(std::unique_ptr<Treatment> t);

    // Renders the untouched original with every treatment applied in
    // order. This is what Measurement, export, and Fiche generation all
    // read from — none of them ever touch the stored original directly.
    cv::Mat render() const;

    const std::filesystem::path& imagePath() const { return imagePath_; }
    const cv::Mat& original() const { return original_; }
    std::size_t treatmentCount() const { return treatments_.size(); }
    // Empty for Vues captured without a CaptureStation (tests, imports) and
    // for sidecars written before ADR 0014.
    const std::optional<CaptureInfo>& captureInfo() const { return captureInfo_; }
    // Why load() could not read this Vue's sidecar; empty when it read fine
    // or there was none. For the UI to tell the user the treatment history
    // was lost (the photo itself is intact).
    const std::optional<std::string>& sidecarError() const { return sidecarError_; }

    std::filesystem::path sidecarPath() const;

private:
    std::filesystem::path imagePath_;
    cv::Mat original_;
    std::vector<std::unique_ptr<Treatment>> treatments_;
    std::optional<CaptureInfo> captureInfo_;
    std::optional<std::string> sidecarError_;
    // True while the file at sidecarPath() is the one load() could not read.
    bool unreadableSidecarOnDisk_ = false;

    void saveSidecar();
    void loadSidecarIfPresent();
};

} // namespace msapp
