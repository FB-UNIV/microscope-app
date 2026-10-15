#pragma once

// Internal to microscope_core (src/, not include/): optional numbers in
// sidecar and workstation-config JSON are written as null when unset.

#include <nlohmann/json.hpp>
#include <optional>

namespace msapp::detail {

inline nlohmann::json optionalToJson(const std::optional<double>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

// A missing or null key reads as unset, so older files still load.
inline std::optional<double> optionalFromJson(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return j.at(key).get<double>();
}

} // namespace msapp::detail
