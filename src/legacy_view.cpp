#include "legacy_view.hpp"

#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace msapp {

namespace {

constexpr std::uint16_t kLegacyPrivateTag = 0xF000;
constexpr std::uint16_t kFieldGrossissement = 0x200;
constexpr std::uint16_t kFieldPixelsPerMm = 0x207;
constexpr std::uint16_t kFieldEnd = 0x7D00;
constexpr std::uint32_t kMaxBlobBytes = 64 * 1024;

class TiffReader {
public:
    explicit TiffReader(std::istream& in) : in_(in) {
        char order[2];
        read(order, 2);
        if (order[0] == 'I' && order[1] == 'I') {
            bigEndian_ = false;
        } else if (order[0] == 'M' && order[1] == 'M') {
            bigEndian_ = true;
        } else {
            throw std::runtime_error("legacy view: not a TIFF (bad byte-order mark)");
        }
        if (u16() != 42) {
            throw std::runtime_error("legacy view: not a TIFF (bad magic)");
        }
    }

    std::uint16_t u16() {
        unsigned char b[2];
        read(b, 2);
        return bigEndian_ ? static_cast<std::uint16_t>(b[0] << 8 | b[1])
                          : static_cast<std::uint16_t>(b[1] << 8 | b[0]);
    }

    std::uint32_t u32() {
        unsigned char b[4];
        read(b, 4);
        return bigEndian_ ? (std::uint32_t(b[0]) << 24 | std::uint32_t(b[1]) << 16 | std::uint32_t(b[2]) << 8 | b[3])
                          : (std::uint32_t(b[3]) << 24 | std::uint32_t(b[2]) << 16 | std::uint32_t(b[1]) << 8 | b[0]);
    }

    void seek(std::uint32_t offset) {
        in_.clear();
        in_.seekg(offset);
        if (!in_) throw std::runtime_error("legacy view: TIFF offset out of range");
    }

    std::vector<unsigned char> bytes(std::uint32_t count) {
        std::vector<unsigned char> out(count);
        if (count > 0) read(out.data(), count);
        return out;
    }

private:
    void read(void* dst, std::size_t n) {
        in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        if (!in_) throw std::runtime_error("legacy view: truncated TIFF");
    }

    std::istream& in_;
    bool bigEndian_ = false;
};

// Returns the raw 0xF000 payload, or empty if the first IFD has no such tag.
std::vector<unsigned char> findPrivateBlob(std::istream& in) {
    TiffReader tiff(in);
    tiff.seek(tiff.u32());
    const std::uint16_t entries = tiff.u16();
    for (std::uint16_t i = 0; i < entries; ++i) {
        const std::uint16_t tag = tiff.u16();
        const std::uint16_t type = tiff.u16();
        const std::uint32_t count = tiff.u32();
        const std::uint32_t valueOrOffset = tiff.u32();
        if (tag != kLegacyPrivateTag) continue;
        if (type != 1 && type != 7) {  // BYTE or UNDEFINED
            throw std::runtime_error("legacy view: tag 0xF000 has unexpected type " + std::to_string(type));
        }
        if (count > kMaxBlobBytes) {
            throw std::runtime_error("legacy view: tag 0xF000 is implausibly large");
        }
        if (count <= 4) {
            std::vector<unsigned char> inline4(4);
            std::memcpy(inline4.data(), &valueOrOffset, 4);  // host order == file order for II on x86
            inline4.resize(count);
            return inline4;
        }
        tiff.seek(valueOrOffset);
        return tiff.bytes(count);
    }
    return {};
}

// The blob is a little-endian list written by the x86 legacy app:
//   u16 marker, u16 pad, u32 total length, then entries
//   { u16 field, u16 type, u32 byte count, value } until field 0x7D00.
// 4-byte values are floats, 2-byte values are int16.
std::map<int, double> parseBlob(const std::vector<unsigned char>& blob) {
    auto le16 = [&](std::size_t at) { return static_cast<std::uint16_t>(blob[at] | blob[at + 1] << 8); };
    auto le32 = [&](std::size_t at) {
        return std::uint32_t(blob[at]) | std::uint32_t(blob[at + 1]) << 8 | std::uint32_t(blob[at + 2]) << 16 |
               std::uint32_t(blob[at + 3]) << 24;
    };

    std::map<int, double> fields;
    std::size_t pos = 8;
    while (pos + 8 <= blob.size()) {
        const std::uint16_t field = le16(pos);
        const std::uint32_t size = le32(pos + 4);
        if (field == kFieldEnd) break;
        if (pos + 8 + size > blob.size()) {
            throw std::runtime_error("legacy view: tag 0xF000 entry overruns its payload");
        }
        const std::size_t value = pos + 8;
        if (size == 4) {
            const std::uint32_t bits = le32(value);
            float f;
            std::memcpy(&f, &bits, sizeof f);
            fields[field] = f;
        } else if (size == 2) {
            fields[field] = static_cast<std::int16_t>(le16(value));
        }
        pos = value + size;
    }
    return fields;
}

} // namespace

std::optional<LegacyViewName> parseLegacyViewFileName(const std::string& fileName) {
    static const std::regex pattern(R"(^([A-Za-z]+?)(\d+)Ech(\d+)Vue(\d+)\.tiff?$)", std::regex::icase);
    std::smatch m;
    if (!std::regex_match(fileName, m, pattern)) return std::nullopt;
    LegacyViewName name;
    name.prefix = m[1].str();
    name.tp = std::stoi(m[2].str());
    name.echantillon = std::stoi(m[3].str());
    name.vue = std::stoi(m[4].str());
    return name;
}

Calibration LegacyViewMetadata::calibration() const {
    std::ostringstream label;
    label << 'x';
    if (grossissement == std::floor(grossissement)) {
        label << static_cast<long long>(grossissement);
    } else {
        label << grossissement;
    }
    return Calibration{label.str(), pixelsPerMm / 1000.0};
}

std::optional<LegacyViewMetadata> parseLegacyViewMetadata(std::istream& tiff) {
    const std::vector<unsigned char> blob = findPrivateBlob(tiff);
    if (blob.size() < 8) return std::nullopt;
    LegacyViewMetadata meta;
    meta.fields = parseBlob(blob);
    const auto g = meta.fields.find(kFieldGrossissement);
    const auto p = meta.fields.find(kFieldPixelsPerMm);
    if (g == meta.fields.end() || p == meta.fields.end()) return std::nullopt;
    meta.grossissement = g->second;
    meta.pixelsPerMm = p->second;
    return meta;
}

std::optional<LegacyViewMetadata> readLegacyViewMetadata(const std::filesystem::path& tiff) {
    std::ifstream in(tiff, std::ios::binary);
    if (!in) {
        throw std::runtime_error("legacy view: cannot open " + tiff.string());
    }
    return parseLegacyViewMetadata(in);
}

LegacyViewImport importLegacyView(const std::filesystem::path& legacyTiff,
                                  const std::filesystem::path& newImagePath) {
    cv::Mat pixels = cv::imread(legacyTiff.string(), cv::IMREAD_UNCHANGED);
    if (pixels.empty()) {
        throw std::runtime_error("importLegacyView: cannot decode " + legacyTiff.string());
    }
    // Everything that can throw runs before the Vue is written: a Vue left
    // behind by a failed import would block any retry onto the same path
    // (immutable-by-path).
    auto metadata = readLegacyViewMetadata(legacyTiff);
    auto name = parseLegacyViewFileName(legacyTiff.filename().string());
    return LegacyViewImport{Vue::capture(pixels, newImagePath), std::move(name), std::move(metadata)};
}

} // namespace msapp
