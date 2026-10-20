#include "image_io.hpp"
#include "safe_write.hpp"

#include <climits>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace msapp {

cv::Mat readImage(const std::filesystem::path& path, int flags) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > INT_MAX) return {};
    std::vector<char> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!in.read(bytes.data(), static_cast<std::streamsize>(size))) return {};
    try {
        // A view of the bytes as the one CV_8U row imdecode reads.
        const cv::Mat encoded(1, static_cast<int>(size), CV_8U, bytes.data());
        return cv::imdecode(encoded, flags);
    } catch (const cv::Exception&) {
        return {};  // a damaged file, as cv::imread reports it
    }
}

void writeImage(const std::filesystem::path& path, const cv::Mat& image, const std::vector<int>& params,
                const std::string& context) {
    std::vector<uchar> encoded;
    bool ok = false;
    try {
        ok = cv::imencode(path.extension().u8string(), image, encoded, params);
    } catch (const cv::Exception& e) {
        throw std::runtime_error(context + ": could not encode " + path.u8string() + ": " + e.what());
    }
    if (!ok) throw std::runtime_error(context + ": could not encode " + path.u8string());
    // The encoded bytes, seen as the chars an ofstream writes.
    const std::string_view bytes(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    detail::writeFileAtomically(path, bytes, context);
}

} // namespace msapp
