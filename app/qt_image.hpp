#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <opencv2/core.hpp>
#include <filesystem>

namespace msapp {

// An 8-bit grey, BGR or BGRA cv::Mat as a QImage that owns its pixels. With
// `fitWithin`, a larger image is first shrunk (INTER_AREA) to fit inside it,
// keeping its aspect ratio.
QImage toQImage(const cv::Mat& image, QSize fitWithin = QSize());

// Paths and QStrings both as UTF-8, so accented Department, TP and
// Échantillon names survive the trip on Windows.
QString toQString(const std::filesystem::path& path);
std::filesystem::path toPath(const QString& text);

} // namespace msapp
