#include "legacy_catalog.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace msapp {

namespace {

// Code points for Windows-1252 bytes 0x80..0x9F (0 = undefined in cp1252).
constexpr std::uint16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

// The legacy loader read one line per field with fixed positions, so the
// reader below mirrors it line by line and reports the line on failure.
class LineReader {
public:
    explicit LineReader(std::istream& in) : in_(in) {}

    std::string next(const char* expected) {
        std::string line;
        if (!std::getline(in_, line)) {
            throw std::runtime_error("legacy catalog: file ends at line " + std::to_string(lineNo_ + 1) +
                                     " while expecting " + expected);
        }
        ++lineNo_;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return cp1252ToUtf8(line);
    }

    int nextInt(const char* expected) {
        const std::string line = next(expected);
        std::istringstream iss(line);
        int value = 0;
        if (!(iss >> value)) {
            throw std::runtime_error("legacy catalog: line " + std::to_string(lineNo_) + " should be " +
                                     expected + ", got '" + line + "'");
        }
        return value;
    }

    int lineNo() const { return lineNo_; }

private:
    std::istream& in_;
    int lineNo_ = 0;
};

} // namespace

std::string cp1252ToUtf8(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (unsigned char b : bytes) {
        if (b < 0x80) {
            out += static_cast<char>(b);
        } else if (b < 0xA0) {
            const std::uint16_t cp = kCp1252High[b - 0x80];
            appendUtf8(out, cp != 0 ? cp : 0xFFFD);
        } else {
            appendUtf8(out, b);  // 0xA0..0xFF map 1:1 to Latin-1 code points
        }
    }
    return out;
}

LegacyCatalog parseLegacyDepartmentCatalog(std::istream& in, const std::string& department) {
    LineReader reader(in);

    // Header "%8x%hd": version in hex, then how many comment lines follow.
    const std::string header = reader.next("the '<version> <comment lines>' header");
    std::istringstream hs(header);
    int version = 0;
    int commentLines = 0;
    if (!(hs >> std::hex >> version >> std::dec >> commentLines) || commentLines < 0) {
        throw std::runtime_error("legacy catalog: bad header '" + header + "'");
    }
    if (version != 4) {
        throw std::runtime_error("legacy catalog: version " + std::to_string(version) +
                                 " not supported (expected 4)");
    }
    for (int i = 0; i < commentLines; ++i) reader.next("a header comment line");
    reader.next("the '//*** utilisateur ***' line");

    LegacyCatalog catalog;
    catalog.department = department;
    catalog.version = version;
    for (int tp = 1; tp <= kLegacyTpsPerDepartment; ++tp) {
        LegacyTp entry;
        entry.slot = tp;
        reader.next("the '//*** Nouveau TP ***' separator");
        entry.name = trimRight(reader.next("the TP name"));
        entry.instructions = reader.next("the TP instructions");
        entry.printCode = reader.nextInt("the print code");
        entry.flags = reader.nextInt("the TP flags");
        reader.next("the '//--- liste Noms Echantillons' line");
        for (int slot = 1; slot <= kLegacyEchantillonsPerTp; ++slot) {
            std::string name = trimRight(reader.next("an Échantillon name"));
            if (!name.empty()) entry.echantillons.push_back({slot, std::move(name)});
        }
        catalog.tps.push_back(std::move(entry));
    }
    return catalog;
}

LegacyCatalog loadLegacyDepartmentCatalog(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("legacy catalog: cannot open " + file.string());
    }
    return parseLegacyDepartmentCatalog(in, file.stem().string());
}

} // namespace msapp
