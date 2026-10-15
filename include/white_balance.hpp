#pragma once

#include <nlohmann/json.hpp>

namespace msapp {

// Per-channel colour gains. Used both as a camera control (hardware gains,
// which UVC cameras don't offer) and as the White reference the
// CaptureStation bakes into every capture (ADR 0014). Normalised to green
// = 1, like the legacy app's per-workstation values (R 1.745, G 1, B 2.576).
struct WhiteBalance {
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;

    bool isIdentity() const { return red == 1.0 && green == 1.0 && blue == 1.0; }

    nlohmann::json toJson() const { return {{"red", red}, {"green", green}, {"blue", blue}}; }

    static WhiteBalance fromJson(const nlohmann::json& j) {
        return WhiteBalance{j.at("red").get<double>(), j.at("green").get<double>(), j.at("blue").get<double>()};
    }
};

} // namespace msapp
