#pragma once

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace msapp {

// A Treatment is one non-destructive step in the image pipeline (ADR 0002:
// treatments are modeled as a generic ordered pipeline, not fixed functions,
// because a teaching tool's treatment list is expected to grow).
//
// A Treatment must never mutate its input; it returns a new image. This is
// what lets Vue keep the original photo untouched on disk (ADR 0003) while
// still supporting an arbitrary, re-orderable, undo-able stack of edits.
class Treatment {
public:
    virtual ~Treatment() = default;

    // Stable identifier written to the sidecar JSON's "type" field and used
    // by TreatmentFactory to reconstruct the right subclass on load.
    virtual std::string type() const = 0;

    // Apply this treatment to `input`, returning the resulting image.
    // Must not modify `input` in place.
    virtual cv::Mat apply(const cv::Mat& input) const = 0;

    // Serialize this treatment's parameters (not the image) for the sidecar.
    // The wrapping {"type": ...} envelope is added by the caller (Vue), not
    // by the treatment itself, so this only needs to return its own params.
    virtual nlohmann::json paramsToJson() const = 0;
};

} // namespace msapp
