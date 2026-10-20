#include "levels_widget.hpp"

#include "theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace msapp {

namespace {

constexpr double kHandleStrip = 14.0;  // under the plot, where the handles sit
constexpr double kGrab = 10.0;         // how near a handle, in pixels, a press takes it

} // namespace

LevelsWidget::LevelsWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);  // the cursor changes over a handle
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::ArrowCursor);
}

void LevelsWidget::setHistogram(const std::array<std::uint64_t, 256>& histogram) {
    // Square root: a photo's peak would flatten every other level.
    const std::uint64_t peak = *std::max_element(histogram.begin(), histogram.end());
    for (std::size_t i = 0; i < 256; ++i) {
        heights_[i] = peak > 0 ? std::sqrt(static_cast<double>(histogram[i]) / static_cast<double>(peak)) : 0.0;
    }
    update();
}

void LevelsWidget::setLevels(int black, int white) {
    black_ = std::clamp(black, 0, 254);
    white_ = std::clamp(white, black_ + 1, 255);
    update();
}

QRectF LevelsWidget::plot() const {
    return QRectF(rect()).adjusted(7, 7, -7, -7 - kHandleStrip);
}

double LevelsWidget::xOf(int level) const {
    const QRectF area = plot();
    return area.left() + (level + 0.5) * area.width() / 256.0;
}

int LevelsWidget::levelAt(double x) const {
    const QRectF area = plot();
    return std::clamp(static_cast<int>(std::floor((x - area.left()) / area.width() * 256.0)), 0, 255);
}

std::optional<LevelsWidget::Handle> LevelsWidget::handleNear(double x) const {
    const double toBlack = std::abs(x - xOf(black_));
    const double toWhite = std::abs(x - xOf(white_));
    if (std::min(toBlack, toWhite) > kGrab) return std::nullopt;
    // Equal distances (handles side by side): the one the press is past.
    if (toBlack == toWhite) return x < xOf(black_) ? Handle::Black : Handle::White;
    return toBlack < toWhite ? Handle::Black : Handle::White;
}

void LevelsWidget::dragTo(double x) {
    const int level = levelAt(x);
    if (dragged_ == Handle::Black) {
        black_ = std::clamp(level, 0, white_ - 1);
    } else {
        white_ = std::clamp(level, black_ + 1, 255);
    }
    update();
    emit levelsDragged(black_, white_);
}

void LevelsWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(theme::border);
    painter.setBrush(theme::canvas);
    painter.drawRoundedRect(frame, 6, 6);

    const QRectF area = plot();
    const double bin = area.width() / 256.0;
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int level = 0; level < 256; ++level) {
        const double h = area.height() * heights_[static_cast<std::size_t>(level)];
        painter.fillRect(QRectF(area.left() + level * bin, area.bottom() - h, bin, h), theme::textMuted);
    }
    // What the stretch sends to black or to white.
    const QColor clipped(0, 0, 0, 140);
    painter.fillRect(QRectF(area.left(), area.top(), xOf(black_) - area.left(), area.height()), clipped);
    painter.fillRect(QRectF(xOf(white_), area.top(), area.right() - xOf(white_), area.height()), clipped);

    // The handles: a line over the plot, a triangle under it.
    painter.setRenderHint(QPainter::Antialiasing);
    const auto handle = [&](int level, const QColor& fill) {
        const double x = xOf(level);
        painter.setPen(QPen(theme::accent, 1));
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        QPainterPath triangle;
        triangle.moveTo(x, area.bottom() + 3);
        triangle.lineTo(x - 6, area.bottom() + kHandleStrip);
        triangle.lineTo(x + 6, area.bottom() + kHandleStrip);
        triangle.closeSubpath();
        painter.setPen(QPen(theme::accent, 1.2));
        painter.setBrush(fill);
        painter.drawPath(triangle);
    };
    handle(black_, Qt::black);
    handle(white_, Qt::white);
}

void LevelsWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const double x = event->position().x();
    // A press away from both handles takes the nearer one there.
    dragged_ = handleNear(x);
    if (!dragged_) dragged_ = std::abs(x - xOf(black_)) <= std::abs(x - xOf(white_)) ? Handle::Black : Handle::White;
    dragTo(x);
}

void LevelsWidget::mouseMoveEvent(QMouseEvent* event) {
    const double x = event->position().x();
    if (dragged_) {
        dragTo(x);
        return;
    }
    setCursor(handleNear(x) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void LevelsWidget::mouseReleaseEvent(QMouseEvent* /*event*/) {
    dragged_.reset();
}

} // namespace msapp
