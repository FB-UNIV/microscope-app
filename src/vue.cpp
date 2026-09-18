#include "vue.hpp"
#include "treatment_factory.hpp"
#include <fstream>
#include <stdexcept>

namespace msapp {

Vue Vue::capture(const cv::Mat& original, const std::filesystem::path& imagePath) {
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
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : treatments_) {
        arr.push_back({{"type", t->type()}, {"params", t->paramsToJson()}});
    }

    std::ofstream out(sidecarPath());
    if (!out) {
        throw std::runtime_error("Vue::saveSidecar: failed to write " + sidecarPath().string());
    }
    out << arr.dump(2);
}

void Vue::loadSidecarIfPresent() {
    auto path = sidecarPath();
    if (!std::filesystem::exists(path)) {
        return; // No treatments applied yet — a valid state, not an error.
    }

    std::ifstream in(path);
    nlohmann::json arr;
    in >> arr;

    auto& factory = TreatmentFactory::instance();
    for (const auto& entry : arr) {
        treatments_.push_back(factory.create(entry.at("type").get<std::string>(), entry.at("params")));
    }
}

} // namespace msapp
