#include "treatment_factory.hpp"
#include "echelle_treatment.hpp"
#include "levels_treatment.hpp"
#include "phase_overlay_treatment.hpp"
#include <stdexcept>

namespace msapp {

namespace {

// Every Treatment that ships in microscope_core. Registered from here rather
// than by static-init registrars in each treatment's .cpp: microscope_core is
// a static library, and the linker leaves out any object file the program
// never references, so a registrar living in one would never run — a program
// that only loads Vues would fail on its first sidecar. Naming each fromJson
// here pulls every treatment in wherever the factory is used.
void registerBuiltinTreatments(TreatmentFactory& factory) {
    factory.registerType("levels", &LevelsTreatment::fromJson);
    factory.registerType("echelle", &EchelleTreatment::fromJson);
    factory.registerType("phase_overlay", &PhaseOverlayTreatment::fromJson);
}

} // namespace

TreatmentFactory& TreatmentFactory::instance() {
    static TreatmentFactory factory = [] {
        TreatmentFactory builtins;
        registerBuiltinTreatments(builtins);
        return builtins;
    }();
    return factory;
}

void TreatmentFactory::registerType(const std::string& type, Creator creator) {
    creators_[type] = std::move(creator);
}

std::unique_ptr<Treatment> TreatmentFactory::create(const std::string& type,
                                                      const nlohmann::json& params) const {
    auto it = creators_.find(type);
    if (it == creators_.end()) {
        throw std::out_of_range("TreatmentFactory: unknown treatment type '" + type +
                                 "' in sidecar — was it added without registering a creator?");
    }
    return it->second(params);
}

} // namespace msapp
