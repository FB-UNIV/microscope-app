#pragma once

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace msapp {

// Reads the legacy Metal app's per-Department catalog files (GMP1.txt,
// CTTM.txt, LPCND.txt, ...) so existing TP / Échantillon structure can seed
// the new config instead of being retyped. Format recovered from the legacy
// loader (0x450a90), version 4 only — the only version we have samples of.
// (The combined DATA_METAL.txt, version 2, is not supported: no sample.)
//
// Files are Windows-1252 with CRLF; every string here is returned as UTF-8.

constexpr int kLegacyTpsPerDepartment = 7;
constexpr int kLegacyEchantillonsPerTp = 8;

struct LegacyEchantillon {
    int slot = 0;          // 1..8 — the "Ech" number in legacy file names (TP1Ech3Vue2.tif)
    std::string name;      // e.g. "A5 : C10"
};

struct LegacyTp {
    int slot = 0;                 // 1..7 — the "TP" number in legacy file names
    std::string name;             // may be empty: legacy kept unused TP slots
    std::string instructions;     // text shown to students
    // Legacy print rule: -1 one print, -2 unlimited, -3 two views once,
    // -4 three views once, -5-N several views N times, 0 per-Échantillon.
    // Superseded by Fiche generation (ADR 0010); kept for the record.
    int printCode = 0;
    int flags = 0;                // bit 0x10 seen on every TP; likely "show Échelle" (inferred)
    std::vector<LegacyEchantillon> echantillons;  // non-empty slots only
};

struct LegacyCatalog {
    std::string department;  // from the file name, e.g. "GMP1"
    int version = 0;
    std::vector<LegacyTp> tps;  // always kLegacyTpsPerDepartment entries, in slot order
};

// Byte-wise Windows-1252 -> UTF-8 (undefined bytes become U+FFFD).
std::string cp1252ToUtf8(const std::string& bytes);

// Throws std::runtime_error naming the line when the file is truncated,
// malformed, or not version 4.
LegacyCatalog parseLegacyDepartmentCatalog(std::istream& in, const std::string& department);

// Department name is the file stem ("GMP1.txt" -> "GMP1").
LegacyCatalog loadLegacyDepartmentCatalog(const std::filesystem::path& file);

} // namespace msapp
