#pragma once

#include "treatment.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace msapp {

// Maps a sidecar JSON "type" string back to the Treatment subclass that
// produced it, so Vue's loader never needs an if/else chain that grows with
// every new treatment — consistent with ADR 0002's whole point.
class TreatmentFactory {
public:
    using Creator = std::function<std::unique_ptr<Treatment>(const nlohmann::json& params)>;

    // The shared factory, with every built-in Treatment already registered
    // (the list is registerBuiltinTreatments() in treatment_factory.cpp — a
    // new Treatment adds its line there).
    static TreatmentFactory& instance();

    // Registers `creator` under `type`, replacing any earlier creator.
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
