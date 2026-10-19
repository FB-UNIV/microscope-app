#include "safe_write.hpp"

#include <fstream>
#include <stdexcept>

namespace msapp::detail {

std::filesystem::path unusedAsidePath(const std::filesystem::path& path, const std::string& suffix) {
    auto candidate = path;
    candidate += suffix;
    for (int n = 2; std::filesystem::exists(candidate); ++n) {
        candidate = path;
        candidate += suffix + "." + std::to_string(n);
    }
    return candidate;
}

void writeFileAtomically(const std::filesystem::path& path, std::string_view content, const std::string& context) {
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (out.fail()) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error(context + ": failed to write " + temp.u8string());
    }
    std::filesystem::rename(temp, path);
}

} // namespace msapp::detail
