#include "qt_image.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace msapp {

QImage toQImage(const cv::Mat& image, QSize fitWithin) {
    cv::Mat shown = image;
    if (fitWithin.isValid() && (image.cols > fitWithin.width() || image.rows > fitWithin.height())) {
        const double scale = std::min(static_cast<double>(fitWithin.width()) / image.cols,
                                      static_cast<double>(fitWithin.height()) / image.rows);
        cv::resize(image, shown, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    if (shown.channels() == 4) {
        cv::cvtColor(shown, shown, cv::COLOR_BGRA2BGR);
    }
    const QImage::Format format = shown.channels() == 1 ? QImage::Format_Grayscale8 : QImage::Format_BGR888;
    // Deep copy: the QImage outlives the cv::Mat.
    return QImage(shown.data, shown.cols, shown.rows, static_cast<qsizetype>(shown.step), format).copy();
}

QString toQString(const std::filesystem::path& path) {
    return QString::fromStdString(path.u8string());
}

std::filesystem::path toPath(const QString& text) {
    return std::filesystem::u8path(text.toStdString());
}

} // namespace msapp
