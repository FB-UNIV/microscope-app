#pragma once

#include "treatment.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace msapp {

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
    static Vue capture(const cv::Mat& original, const std::filesystem::path& imagePath);

    // Loads an existing Vue: reads the original image, plus its sidecar
    // (imagePath + ".json") if one exists — a Vue with no treatments yet
    // simply has no sidecar file, which is a valid state, not an error.
    static Vue load(const std::filesystem::path& imagePath);

    // Appends a treatment to the pipeline and persists the updated sidecar
    // immediately (no separate "save" step — matches the sidecar being the
    // single source of truth for what's applied).
    void addTreatment(std::unique_ptr<Treatment> t);

    // Renders the untouched original with every treatment applied in
    // order. This is what Measurement, export, and Fiche generation all
    // read from — none of them ever touch the stored original directly.
    cv::Mat render() const;

    const std::filesystem::path& imagePath() const { return imagePath_; }
    const cv::Mat& original() const { return original_; }
    std::size_t treatmentCount() const { return treatments_.size(); }

    std::filesystem::path sidecarPath() const;

private:
    std::filesystem::path imagePath_;
    cv::Mat original_;
    std::vector<std::unique_ptr<Treatment>> treatments_;

    void saveSidecar() const;
    void loadSidecarIfPresent();
};

} // namespace msapp
