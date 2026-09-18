#pragma once

#include "treatment.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace msapp {

// Maps a sidecar JSON "type" string back to the Treatment subclass that
// produced it. Each Treatment implementation self-registers a creator
// (see the bottom of levels_treatment.cpp / echelle_treatment.cpp) so Vue's
// loader never needs an if/else chain that grows with every new treatment —
// consistent with ADR 0002's whole point.
class TreatmentFactory {
public:
    using Creator = std::function<std::unique_ptr<Treatment>(const nlohmann::json& params)>;

    static TreatmentFactory& instance();

    // Registers `creator` under `type`. Called once per Treatment subclass,
    // at static-init time, from that subclass's own .cpp file.
    void registerType(const std::string& type, Creator creator);

    // Reconstructs a Treatment from its sidecar "type" + params. Throws
    // std::out_of_range if `type` was never registered — a sidecar
    // referencing an unknown treatment type is a real error, not silently
    // ignorable, since silently dropping a treatment would change what the
    // Vue renders as.
    std::unique_ptr<Treatment> create(const std::string& type, const nlohmann::json& params) const;

private:
    std::unordered_map<std::string, Creator> creators_;
};

} // namespace msapp
