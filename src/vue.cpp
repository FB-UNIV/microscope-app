#include "vue.hpp"
#include "treatment_factory.hpp"
#include <fstream>
#include <stdexcept>

namespace msapp {

namespace {

nlohmann::json optionalToJson(const std::optional<double>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

std::optional<double> optionalFromJson(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return j.at(key).get<double>();
}

} // namespace

nlohmann::json CaptureInfo::toJson() const {
    return {
        {"camera_backend", cameraBackend},
        {"exposure_ms", optionalToJson(exposureMs)},
        {"white_balance_kelvin", optionalToJson(whiteBalanceKelvin)},
        {"white_reference", whiteReference.toJson()},
    };
}

CaptureInfo CaptureInfo::fromJson(const nlohmann::json& j) {
    CaptureInfo info;
    info.cameraBackend = j.value("camera_backend", std::string{});
    info.exposureMs = optionalFromJson(j, "exposure_ms");
    info.whiteBalanceKelvin = optionalFromJson(j, "white_balance_kelvin");
    if (j.contains("white_reference")) info.whiteReference = WhiteBalance::fromJson(j.at("white_reference"));
    return info;
}

Vue Vue::capture(const cv::Mat& original, const std::filesystem::path& imagePath,
                 std::optional<CaptureInfo> captureInfo) {
    if (std::filesystem::exists(imagePath)) {
        throw std::runtime_error("Vue::capture: " + imagePath.string() +
                                  " already exists — Vues are immutable-by-path, "
                                  "generate a fresh unique path instead of reusing one");
    }
    if (!cv::imwrite(imagePath.string(), original)) {
        throw std::runtime_error("Vue::capture: failed to write " + imagePath.string());
    }

    Vue vue;
    vue.imagePath_ = imagePath;
    vue.original_ = original.clone();
    vue.captureInfo_ = std::move(captureInfo);
    if (vue.captureInfo_) {
        vue.saveSidecar();
    }
    return vue;
}

Vue Vue::load(const std::filesystem::path& imagePath) {
    cv::Mat img = cv::imread(imagePath.string(), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        throw std::runtime_error("Vue::load: failed to read " + imagePath.string());
    }

    Vue vue;
    vue.imagePath_ = imagePath;
    vue.original_ = img;
    vue.loadSidecarIfPresent();
    return vue;
}

void Vue::addTreatment(std::unique_ptr<Treatment> t) {
    treatments_.push_back(std::move(t));
    saveSidecar();
}

cv::Mat Vue::render() const {
    cv::Mat result = original_.clone();
    for (const auto& t : treatments_) {
        result = t->apply(result);
    }
    return result;
}

std::filesystem::path Vue::sidecarPath() const {
    auto path = imagePath_;
    path += ".json";
    return path;
}

void Vue::saveSidecar() const {
    nlohmann::json treatments = nlohmann::json::array();
    for (const auto& t : treatments_) {
        treatments.push_back({{"type", t->type()}, {"params", t->paramsToJson()}});
    }
    nlohmann::json sidecar = {{"treatments", treatments}};
    if (captureInfo_) {
        sidecar["capture"] = captureInfo_->toJson();
    }

    std::ofstream out(sidecarPath());
    if (!out) {
        throw std::runtime_error("Vue::saveSidecar: failed to write " + sidecarPath().string());
    }
    out << sidecar.dump(2);
}

void Vue::loadSidecarIfPresent() {
    auto path = sidecarPath();
    if (!std::filesystem::exists(path)) {
        return; // No treatments applied yet — a valid state, not an error.
    }

    std::ifstream in(path);
    nlohmann::json sidecar;
    in >> sidecar;

    // Sidecars written before ADR 0014 are a bare array of treatments.
    const nlohmann::json& treatments = sidecar.is_array() ? sidecar : sidecar.at("treatments");
    if (sidecar.is_object() && sidecar.contains("capture")) {
        captureInfo_ = CaptureInfo::fromJson(sidecar.at("capture"));
    }

    auto& factory = TreatmentFactory::instance();
    for (const auto& entry : treatments) {
        treatments_.push_back(factory.create(entry.at("type").get<std::string>(), entry.at("params")));
    }
}

} // namespace msapp
