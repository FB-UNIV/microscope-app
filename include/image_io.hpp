#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace msapp {

// Image files read and written through the path itself (review H1):
// cv::imread and cv::imwrite take a narrow string and open it with fopen,
// which on Windows is the ANSI code page, so an Échantillon named "Fonte γ"
// or "Acier α+β" could not be captured, loaded or shown. These open a
// std::ifstream / std::ofstream on the std::filesystem::path (wide on
// Windows) and decode or encode in memory.

// Decodes the file at `path` with cv::imdecode `flags` (cv::IMREAD_*).
// Like cv::imread, returns an empty Mat when the file can't be read or
// decoded.
cv::Mat readImage(const std::filesystem::path& path, int flags = cv::IMREAD_COLOR);

// Encodes `image` in the format of `path`'s extension (as cv::imwrite
// picks it), with cv::imwrite `params`, and writes it through <path>.tmp
// renamed over `path`: a crash or full disk mid-write never leaves half an
// image (ADR 0004). Replaces an existing file: callers that must not, such
// as Vue::capture, check first. Throws std::runtime_error starting with
// `context` when the image can't be encoded or written.
void writeImage(const std::filesystem::path& path, const cv::Mat& image, const std::vector<int>& params,
                const std::string& context);

} // namespace msapp
