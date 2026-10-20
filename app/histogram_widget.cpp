#include "histogram_widget.hpp"

#include "theme.hpp"

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
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(theme::border);
    painter.setBrush(theme::canvas);
    painter.drawRoundedRect(frame, 6, 6);
    if (threshold_ < 0) {
        painter.setPen(theme::textFaint);
        painter.drawText(rect(), Qt::AlignCenter, tr("Histogramme après CALCUL"));
        return;
    }

    // Inside the frame, clear of its rounded corners.
    const QRectF plot = frame.adjusted(6, 6, -6, -6);
    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const double binWidth = plot.width() / 256.0;
    const auto x = [&](int level) { return plot.left() + level * binWidth; };

    const int low = std::max(0, threshold_ - tolerance_);
    const int high = std::min(255, threshold_ + tolerance_);
    QColor band = theme::accent;
    band.setAlpha(56);
    painter.fillRect(QRectF(x(low), plot.top(), x(high + 1) - x(low), plot.height()), band);

    const std::uint32_t peak = *std::max_element(histogram_.begin(), histogram_.end());
    if (peak > 0) {
        for (int level = 0; level < 256; ++level) {
            const double barHeight = plot.height() * static_cast<double>(histogram_[level]) / peak;
            painter.fillRect(QRectF(x(level), plot.bottom() - barHeight, binWidth, barHeight), theme::textMuted);
        }
    }
    painter.setPen(QPen(theme::accent, 2));
    const double at = x(threshold_) + binWidth / 2;
    painter.drawLine(QPointF(at, plot.top()), QPointF(at, plot.bottom()));
}

} // namespace msapp
