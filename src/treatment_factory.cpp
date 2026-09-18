#include "treatment_factory.hpp"
#include <stdexcept>

namespace msapp {

TreatmentFactory& TreatmentFactory::instance() {
    static TreatmentFactory factory;
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
