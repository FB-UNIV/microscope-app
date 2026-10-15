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

void writeFileAtomically(const std::filesystem::path& path, const std::string& content, const std::string& context) {
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    if (out.fail()) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error(context + ": failed to write " + temp.string());
    }
    std::filesystem::rename(temp, path);
}

} // namespace msapp::detail
