#include "preview_widget.hpp"

#include "theme.hpp"

#include <QEasingCurve>
#include <QFontMetrics>
#include <QPainter>
#include <QVariantAnimation>

#include <algorithm>

namespace msapp {

namespace {

constexpr int kSpinnerSize = 28;
constexpr int kMessageWidth = 560;

} // namespace

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent), spinner_(new QVariantAnimation(this)), flash_(new QVariantAnimation(this)) {
    setAttribute(Qt::WA_OpaquePaintEvent);  // paintEvent fills every pixel
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    spinner_->setStartValue(0.0);
    spinner_->setEndValue(360.0);
    spinner_->setDuration(900);
    spinner_->setLoopCount(-1);
    connect(spinner_, &QVariantAnimation::valueChanged, this, [this] { update(); });

    // Quick to appear, eased out: a shutter, not a fade.
    flash_->setStartValue(1.0);
    flash_->setEndValue(0.0);
    flash_->setDuration(260);
    flash_->setEasingCurve(QEasingCurve::OutCubic);
    connect(flash_, &QVariantAnimation::valueChanged, this, [this] { update(); });
}

void PreviewWidget::setFrame(const QImage& frame) {
    frame_ = frame;
    message_.clear();
    spinner_->stop();
    update();
}

void PreviewWidget::showMessage(const QString& message) {
    frame_ = QImage();
    message_ = message;
    spinner_->stop();
    update();
}

void PreviewWidget::showBusy(const QString& message) {
    showMessage(message);
    spinner_->start();
}

void PreviewWidget::flash() {
    flash_->stop();
    flash_->start();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), theme::canvas);
    if (frame_.isNull()) {
        paintMessage(painter);
        return;
    }
    QRect target(QPoint(0, 0), frame_.size().scaled(size(), Qt::KeepAspectRatio));
    target.moveCenter(rect().center());
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, frame_);
    // A faint neutral edge, so a dark photo doesn't melt into the canvas.
    painter.setPen(QColor(255, 255, 255, 20));
    painter.drawRect(target.adjusted(0, 0, -1, -1));
    if (flash_->state() == QAbstractAnimation::Running) {
        QColor white(Qt::white);
        white.setAlphaF(0.35 * flash_->currentValue().toDouble());
        painter.fillRect(target, white);
    }
}

void PreviewWidget::paintMessage(QPainter& painter) {
    const int split = message_.indexOf(QStringLiteral("\n\n"));
    const QString title = split < 0 ? message_ : message_.left(split);
    const QString details = split < 0 ? QString() : message_.mid(split + 2).trimmed();

    QFont titleFont = font();
    if (titleFont.pointSizeF() > 0) titleFont.setPointSizeF(titleFont.pointSizeF() * 1.35);
    titleFont.setWeight(QFont::DemiBold);
    const QFont detailsFont = font();

    const int width = std::min(kMessageWidth, this->width() - 64);
    const int flags = Qt::AlignHCenter | Qt::TextWordWrap;
    const QRect titleBox = QFontMetrics(titleFont).boundingRect(QRect(0, 0, width, 10000), flags, title);
    const QRect detailsBox = details.isEmpty()
                                 ? QRect()
                                 : QFontMetrics(detailsFont).boundingRect(QRect(0, 0, width, 10000), flags, details);
    const bool busy = spinner_->state() == QAbstractAnimation::Running;
    const int spinnerHeight = busy ? kSpinnerSize + 18 : 0;
    const int detailsGap = details.isEmpty() ? 0 : 10;
    const int total = spinnerHeight + titleBox.height() + detailsGap + detailsBox.height();
    const int left = (this->width() - width) / 2;
    int y = (height() - total) / 2;

    painter.setRenderHint(QPainter::Antialiasing);
    if (busy) {
        const QRectF ring((this->width() - kSpinnerSize) / 2.0, y, kSpinnerSize, kSpinnerSize);
        painter.setPen(QPen(theme::border, 3));
        painter.drawEllipse(ring);
        QPen arc(theme::accent, 3);
        arc.setCapStyle(Qt::RoundCap);
        painter.setPen(arc);
        const int start = static_cast<int>(-spinner_->currentValue().toDouble() * 16);
        painter.drawArc(ring, start, 100 * 16);
        y += spinnerHeight;
    }
    painter.setFont(titleFont);
    painter.setPen(theme::text);
    painter.drawText(QRect(left, y, width, titleBox.height()), flags, title);
    y += titleBox.height() + detailsGap;
    if (!details.isEmpty()) {
        painter.setFont(detailsFont);
        painter.setPen(theme::textMuted);
        painter.drawText(QRect(left, y, width, detailsBox.height()), flags, details);
    }
}

} // namespace msapp
