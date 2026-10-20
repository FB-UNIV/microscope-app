#pragma once

#include "calibration.hpp"
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
    // Values the camera accepted (ADR 0015); empty when it refused them or
    // ran on auto — unknown either way.
    std::optional<double> exposureMs;
    std::optional<double> whiteBalanceKelvin;
    WhiteBalance whiteReference;
    int frameWidth = 0;          // frame as grabbed; 0 = not recorded
    int frameHeight = 0;         //   (sidecars written before ADR 0015)
    std::string pixelFormat;     // negotiated FOURCC; empty = unknown

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
    // Captures a new Vue: writes `original` unmodified to `imagePath`
    // (through a temp file, so never half a photo) and returns a Vue with
    // an empty treatment stack. Fails (throws) if
    // `imagePath` already exists — Vues are immutable-by-path (see the
    // race/overwrite discussion behind ADR 0004); callers must generate a
    // fresh unique path per capture, never reuse one.
    // With captureInfo or calibration, the sidecar is written immediately so
    // they exist on disk even before any Treatment is added.
    // `calibration` is the Grossissement's Calibration, fixed for this Vue's
    // lifetime (ADR 0015). Throws std::runtime_error, writing nothing, when
    // it was measured at another frame size than `original`.
    static Vue capture(const cv::Mat& original, const std::filesystem::path& imagePath,
                       std::optional<CaptureInfo> captureInfo = std::nullopt,
                       std::optional<Calibration> calibration = std::nullopt);

    // Loads an existing Vue: reads the original image, plus its sidecar
    // (imagePath + ".json") if one exists — a Vue with no treatments yet
    // simply has no sidecar file, which is a valid state, not an error.
    // A sidecar that exists but cannot be read (corrupt JSON, unknown
    // treatment type, bad params) does not stop the photo loading (ADR 0003):
    // the Vue comes back with no treatments and no capture info, and
    // sidecarError() says why. A sidecar Calibration, or an Échelle, measured
    // at another frame size than the image costs only itself (ADR 0015): it
    // is dropped and named in sidecarError(), while the photo, the other
    // treatments and the capture info load. Only an unreadable image throws.
    // Treatments work on 8 bits: a 16-bit image (a TIFF from another
    // program) loads scaled to 8 bits, its file untouched; any other depth
    // throws std::runtime_error.
    static Vue load(const std::filesystem::path& imagePath);

    // Appends a treatment to the pipeline and persists the updated sidecar
    // immediately (no separate "save" step — matches the sidecar being the
    // single source of truth for what's applied). The sidecar is replaced
    // atomically; if load() could not read it, or dropped part of it, it is
    // first kept aside as sidecarPath() + ".unreadable" or ".mismatched"
    // rather than overwritten (ADR 0004). Throws std::runtime_error, saving
    // nothing, for a treatment that can't apply to this image's frame size
    // (an Échelle measured at another one, ADR 0015). When the write fails,
    // the treatment is taken out again and the error rethrown.
    void addTreatment(std::unique_ptr<Treatment> t);

    // Editing the pipeline (ADR 0002: re-orderable, undo-able), e.g. Levels
    // set again, or before the Échelle so the bar keeps its contrast. Each
    // persists the sidecar like addTreatment(), in one write; when that
    // write fails, the pipeline is left as it was and the error is rethrown.
    // insertTreatment() and replaceTreatment() refuse a mismatched treatment
    // like addTreatment(); all three throw std::out_of_range for an index
    // past the pipeline (insert accepts treatmentCount(): append).
    void insertTreatment(std::size_t index, std::unique_ptr<Treatment> t);
    void replaceTreatment(std::size_t index, std::unique_ptr<Treatment> t);
    void removeTreatment(std::size_t index);

    // The pipeline as the sidecar records it, [{"type": …, "params": …}, …]:
    // a snapshot to come back to, for the viewer's undo and redo (#93).
    nlohmann::json treatmentsJson() const;
    // Puts back a pipeline treatmentsJson() gave, saved in one write. Throws
    // like addTreatment(), leaving the pipeline as it was, when a Treatment
    // can't be rebuilt or can't apply to this photo, or the write fails.
    void setTreatments(const nlohmann::json& treatments);

    // The index-th treatment, in apply order. Throws std::out_of_range.
    const Treatment& treatment(std::size_t index) const { return *treatments_.at(index); }

    // Renders the untouched original with every treatment applied in
    // order. This is what Measurement and export read from; the Fiche
    // applies only the Échelle (fichePhoto(), ADR 0022). None of them ever
    // touch the stored original.
    cv::Mat render() const;

    // Exports a flattened copy (issue #28): render() written to `target` as
    // PNG, TIFF or JPEG (by its extension; JPEG at quality 95), through
    // `target` + ".tmp" so a failed write never leaves half an image. Saving and
    // exporting stay different actions (ADR 0003): throws
    // std::invalid_argument, writing nothing, when `target` is this Vue's
    // own photo or sidecar or has another extension, and std::runtime_error
    // when the file can't be written.
    void exportRendered(const std::filesystem::path& target) const;

    const std::filesystem::path& imagePath() const { return imagePath_; }
    const cv::Mat& original() const { return original_; }
    std::size_t treatmentCount() const { return treatments_.size(); }
    // Empty for Vues captured without a CaptureStation (tests, imports) and
    // for sidecars written before ADR 0014.
    const std::optional<CaptureInfo>& captureInfo() const { return captureInfo_; }
    // The Calibration this Vue was captured with, checked against its frame
    // size — what Measurement, Échelle and Vickers on this Vue use.
    // Empty when none was given, for sidecars written before ADR 0015, and
    // when load() dropped one measured at another frame size.
    const std::optional<Calibration>& calibration() const { return calibration_; }
    // What load() could not use from this Vue's sidecar: the whole file when
    // it was unreadable, or a Calibration or Échelle measured at another
    // frame size (both sizes named). Empty when it read fine or there was
    // none. For the UI to tell the user what was lost (the photo itself is
    // intact, and the file is kept aside on the next save).
    const std::optional<std::string>& sidecarError() const { return sidecarError_; }

    std::filesystem::path sidecarPath() const;

private:
    std::filesystem::path imagePath_;
    cv::Mat original_;
    std::vector<std::unique_ptr<Treatment>> treatments_;
    std::optional<CaptureInfo> captureInfo_;
    std::optional<Calibration> calibration_;
    std::optional<std::string> sidecarError_;
    // Non-empty while the file at sidecarPath() holds something load() did
    // not keep: ".unreadable" or ".mismatched", the suffix saveSidecar()
    // moves it aside to before writing.
    std::string setAsideSuffix_;

    void saveSidecar();
    void loadSidecarIfPresent();
};

} // namespace msapp
