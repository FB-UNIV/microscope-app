#include "preview_widget.hpp"

#include <QPainter>

namespace msapp {

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);  // paintEvent fills every pixel
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewWidget::setFrame(const QImage& frame) {
    frame_ = frame;
    message_.clear();
    update();
}

void PreviewWidget::showMessage(const QString& message) {
    frame_ = QImage();
    message_ = message;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!frame_.isNull()) {
        QRect target(QPoint(0, 0), frame_.size().scaled(size(), Qt::KeepAspectRatio));
        target.moveCenter(rect().center());
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, frame_);
        return;
    }
    painter.setPen(Qt::white);
    QFont font = painter.font();
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.3);  // -1 for a pixel-sized font
    painter.setFont(font);
    painter.drawText(rect().adjusted(32, 32, -32, -32), Qt::AlignCenter | Qt::TextWordWrap, message_);
}

} // namespace msapp
