#pragma once

// Internal to microscope_core (src/, not include/): writes that never destroy
// an earlier file (ADR 0004), shared by Vue sidecars and the workstation
// config.

#include <filesystem>
#include <string>

namespace msapp::detail {

// First free name among <path><suffix>, <path><suffix>.2, ... — where a file
// that must not be overwritten is moved aside.
std::filesystem::path unusedAsidePath(const std::filesystem::path& path, const std::string& suffix);

// Writes `content` to <path>.tmp, then renames it over `path`: a crash or
// full disk mid-write leaves the previous file, never a truncated one.
// Throws std::runtime_error starting with `context` when the write fails.
void writeFileAtomically(const std::filesystem::path& path, const std::string& content, const std::string& context);

} // namespace msapp::detail
