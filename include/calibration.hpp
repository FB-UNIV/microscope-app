#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace msapp {

// Calibration (see CONTEXT.md): the pixels-per-µm ratio tied to one
// Grossissement. Set once per objective against a mire reticle, reused for
// every future photo taken at that Grossissement — this struct is that
// reusable value, not a per-photo measurement.
struct Calibration {
    std::string grossissement;   // e.g. "x40" — which objective this applies to
    double pixelsPerMicron = 0.0;

    nlohmann::json toJson() const {
        return {{"grossissement", grossissement}, {"pixels_per_micron", pixelsPerMicron}};
    }

    static Calibration fromJson(const nlohmann::json& j) {
        Calibration c;
        c.grossissement = j.at("grossissement").get<std::string>();
        c.pixelsPerMicron = j.at("pixels_per_micron").get<double>();
        return c;
    }
};

} // namespace msapp
