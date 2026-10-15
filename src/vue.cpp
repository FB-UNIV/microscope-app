#include "vue.hpp"
#include "json_optional.hpp"
#include "safe_write.hpp"
#include "treatment_factory.hpp"
#include <opencv2/imgcodecs.hpp>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace msapp {

using detail::optionalFromJson;
using detail::optionalToJson;
using detail::unusedAsidePath;
using detail::writeFileAtomically;

nlohmann::json CaptureInfo::toJson() const {
    return {
        {"camera_backend", cameraBackend},
        {"exposure_ms", optionalToJson(exposureMs)},
        {"white_balance_kelvin", optionalToJson(whiteBalanceKelvin)},
        {"white_reference", whiteReference.toJson()},
        {"frame_width", frameWidth},
        {"frame_height", frameHeight},
        {"pixel_format", pixelFormat},
    };
}

CaptureInfo CaptureInfo::fromJson(const nlohmann::json& j) {
    CaptureInfo info;
    info.cameraBackend = j.value("camera_backend", std::string{});
    info.exposureMs = optionalFromJson(j, "exposure_ms");
    info.whiteBalanceKelvin = optionalFromJson(j, "white_balance_kelvin");
    if (j.contains("white_reference")) info.whiteReference = WhiteBalance::fromJson(j.at("white_reference"));
    info.frameWidth = j.value("frame_width", 0);
    info.frameHeight = j.value("frame_height", 0);
    info.pixelFormat = j.value("pixel_format", std::string{});
    return info;
}

Vue Vue::capture(const cv::Mat& original, const std::filesystem::path& imagePath,
                 std::optional<CaptureInfo> captureInfo, std::optional<Calibration> calibration) {
    if (std::filesystem::exists(imagePath)) {
        throw std::runtime_error("Vue::capture: " + imagePath.string() +
                                  " already exists — Vues are immutable-by-path, "
                                  "generate a fresh unique path instead of reusing one");
    }
    // Before anything is written: a Vue must never be stored with a
    // Calibration that doesn't hold for its pixels (ADR 0015).
    if (calibration) {
        calibration->requireFitsFrame(original.cols, original.rows, "Vue::capture");
    }
    if (!cv::imwrite(imagePath.string(), original)) {
        throw std::runtime_error("Vue::capture: failed to write " + imagePath.string());
    }

    Vue vue;
    vue.imagePath_ = imagePath;
    vue.original_ = original.clone();
    vue.captureInfo_ = std::move(captureInfo);
    vue.calibration_ = std::move(calibration);
    if (vue.captureInfo_ || vue.calibration_) {
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
    // Refused before it is saved: render() could never apply it (ADR 0015).
    if (auto why = t->frameMismatch(original_.cols, original_.rows)) {
        throw std::runtime_error("Vue::addTreatment: " + *why);
    }
    treatments_.push_back(std::move(t));
    saveSidecar();
}

namespace {

void requireIndex(std::size_t index, std::size_t size, const char* context) {
    if (index >= size) {
        throw std::out_of_range(std::string(context) + ": no treatment at index " + std::to_string(index) +
                                ", the Vue has " + std::to_string(size));
    }
}

} // namespace

void Vue::insertTreatment(std::size_t index, std::unique_ptr<Treatment> t) {
    requireIndex(index, treatments_.size() + 1, "Vue::insertTreatment");
    if (auto why = t->frameMismatch(original_.cols, original_.rows)) {
        throw std::runtime_error("Vue::insertTreatment: " + *why);
    }
    const auto at = treatments_.begin() + static_cast<std::ptrdiff_t>(index);
    treatments_.insert(at, std::move(t));
    try {
        saveSidecar();
    } catch (...) {
        treatments_.erase(treatments_.begin() + static_cast<std::ptrdiff_t>(index));
        throw;
    }
}

void Vue::replaceTreatment(std::size_t index, std::unique_ptr<Treatment> t) {
    requireIndex(index, treatments_.size(), "Vue::replaceTreatment");
    if (auto why = t->frameMismatch(original_.cols, original_.rows)) {
        throw std::runtime_error("Vue::replaceTreatment: " + *why);
    }
    std::swap(treatments_[index], t);
    try {
        saveSidecar();
    } catch (...) {
        std::swap(treatments_[index], t);
        throw;
    }
}

void Vue::removeTreatment(std::size_t index) {
    requireIndex(index, treatments_.size(), "Vue::removeTreatment");
    const auto at = treatments_.begin() + static_cast<std::ptrdiff_t>(index);
    std::unique_ptr<Treatment> removed = std::move(*at);
    treatments_.erase(at);
    try {
        saveSidecar();
    } catch (...) {
        treatments_.insert(treatments_.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        throw;
    }
}

cv::Mat Vue::render() const {
    cv::Mat result = original_.clone();
    for (const auto& t : treatments_) {
        result = t->apply(result);
    }
    return result;
}

void Vue::exportRendered(const std::filesystem::path& target) const {
    std::string extension = target.extension().string();
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<int> params;
    if (extension == ".jpg" || extension == ".jpeg") {
        params = {cv::IMWRITE_JPEG_QUALITY, 95};
    } else if (extension != ".png" && extension != ".tif" && extension != ".tiff") {
        throw std::invalid_argument("Vue::exportRendered: " + target.string() +
                                    ": export as .png, .tif, .tiff, .jpg or .jpeg");
    }
    // The stored photo and its sidecar are never overwritten (ADR 0003).
    std::error_code error;
    for (const auto& own : {imagePath_, sidecarPath()}) {
        if (std::filesystem::exists(target, error) && std::filesystem::exists(own, error) &&
            std::filesystem::equivalent(target, own, error)) {
            throw std::invalid_argument("Vue::exportRendered: " + target.string() +
                                        " is this Vue's own file; export to another name");
        }
    }

    // <stem>.tmp<ext>: OpenCV picks the format from the extension.
    auto temp = target;
    temp.replace_extension();
    temp += ".tmp";
    temp += target.extension();
    if (!cv::imwrite(temp.string(), render(), params)) {
        std::filesystem::remove(temp, error);
        throw std::runtime_error("Vue::exportRendered: failed to write " + target.string());
    }
    std::filesystem::rename(temp, target);
}

std::filesystem::path Vue::sidecarPath() const {
    auto path = imagePath_;
    path += ".json";
    return path;
}

void Vue::saveSidecar() {
    nlohmann::json treatments = nlohmann::json::array();
    for (const auto& t : treatments_) {
        treatments.push_back({{"type", t->type()}, {"params", t->paramsToJson()}});
    }
    nlohmann::json sidecar = {{"treatments", treatments}};
    if (captureInfo_) {
        sidecar["capture"] = captureInfo_->toJson();
    }
    if (calibration_) {
        sidecar["calibration"] = calibration_->toJson();
    }

    const auto path = sidecarPath();
    // Never overwrite a sidecar load() could not read, or dropped part of: it
    // may still be the only record of this Vue's treatments, capture settings
    // or Calibration (ADR 0004 — no write destroys an earlier file).
    if (!setAsideSuffix_.empty()) {
        if (std::filesystem::exists(path)) {
            std::filesystem::rename(path, unusedAsidePath(path, setAsideSuffix_));
        }
        setAsideSuffix_.clear();
    }

    // A crash or full disk mid-write leaves the previous sidecar, never a
    // truncated one.
    writeFileAtomically(path, sidecar.dump(2), "Vue::saveSidecar");
}

void Vue::loadSidecarIfPresent() {
    auto path = sidecarPath();
    if (!std::filesystem::exists(path)) {
        return; // No treatments applied yet — a valid state, not an error.
    }

    // Built aside and kept only if the whole sidecar reads: half a treatment
    // stack would render differently from what was saved.
    std::vector<std::unique_ptr<Treatment>> treatments;
    std::optional<CaptureInfo> captureInfo;
    std::optional<Calibration> calibration;
    try {
        std::ifstream in(path);
        nlohmann::json sidecar;
        in >> sidecar;

        // Sidecars written before ADR 0014 are a bare array of treatments, and
        // ones written before ADR 0015 have no "calibration": both load as
        // uncalibrated Vues.
        const nlohmann::json& entries = sidecar.is_array() ? sidecar : sidecar.at("treatments");
        if (sidecar.is_object() && sidecar.contains("capture")) {
            captureInfo = CaptureInfo::fromJson(sidecar.at("capture"));
        }
        if (sidecar.is_object() && sidecar.contains("calibration") && !sidecar.at("calibration").is_null()) {
            calibration = Calibration::fromJson(sidecar.at("calibration"));
        }

        auto& factory = TreatmentFactory::instance();
        for (const auto& entry : entries) {
            treatments.push_back(factory.create(entry.at("type").get<std::string>(), entry.at("params")));
        }
    } catch (const std::exception& e) {
        // Losing the sidecar never loses the photo (ADR 0003): load it bare
        // and report why. The file is left as is; saveSidecar() sets it aside.
        sidecarError_ = path.string() + ": " + e.what();
        setAsideSuffix_ = ".unreadable";
        return;
    }

    // A Calibration or an Échelle measured at another frame size no longer
    // describes this photo, but it costs only itself (ADR 0003 over ADR
    // 0015): it is dropped and reported, the rest loads, and the file that
    // still holds it is set aside on the next save. Dropping an Échelle
    // removes only its bar, so the other treatments render as saved.
    std::string dropped;
    auto drop = [&](const std::string& what, const std::string& why) {
        dropped += (dropped.empty() ? "" : "; ") + what + " dropped: " + why;
    };
    if (calibration) {
        if (auto why = calibration->frameMismatch(original_.cols, original_.rows)) {
            drop("calibration", *why);
            calibration.reset();
        }
    }
    std::vector<std::unique_ptr<Treatment>> usable;
    for (auto& t : treatments) {
        if (auto why = t->frameMismatch(original_.cols, original_.rows)) {
            drop(t->type() + " treatment", *why);
        } else {
            usable.push_back(std::move(t));
        }
    }
    if (!dropped.empty()) {
        sidecarError_ = path.string() + ": " + dropped;
        setAsideSuffix_ = ".mismatched";
    }
    treatments_ = std::move(usable);
    captureInfo_ = std::move(captureInfo);
    calibration_ = std::move(calibration);
}

} // namespace msapp
