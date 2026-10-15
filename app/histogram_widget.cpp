#include "histogram_widget.hpp"

#include <QPainter>

#include <algorithm>

namespace msapp {

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(256, 100);
}

void HistogramWidget::setHistogram(const std::array<std::uint32_t, 256>& histogram, int threshold, int tolerance) {
    histogram_ = histogram;
    threshold_ = threshold;
    tolerance_ = tolerance;
    update();
}

void HistogramWidget::clear() {
    threshold_ = -1;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    if (threshold_ < 0) return;

    const double binWidth = width() / 256.0;
    const auto x = [&](int level) { return level * binWidth; };

    const int low = std::max(0, threshold_ - tolerance_);
    const int high = std::min(255, threshold_ + tolerance_);
    painter.fillRect(QRectF(x(low), 0, x(high + 1) - x(low), height()), QColor(255, 224, 178));

    const std::uint32_t peak = *std::max_element(histogram_.begin(), histogram_.end());
    if (peak > 0) {
        for (int level = 0; level < 256; ++level) {
            const double barHeight = height() * static_cast<double>(histogram_[level]) / peak;
            painter.fillRect(QRectF(x(level), height() - barHeight, binWidth, barHeight), QColor(90, 90, 90));
        }
    }
    painter.setPen(QPen(Qt::red, 2));
    painter.drawLine(QPointF(x(threshold_) + binWidth / 2, 0), QPointF(x(threshold_) + binWidth / 2, height()));
}

} // namespace msapp
