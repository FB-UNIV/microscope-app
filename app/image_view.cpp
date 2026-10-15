#include "image_view.hpp"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QWheelEvent>

namespace msapp {

namespace {

QPen markPen() {
    QPen pen(QColor(255, 40, 40));
    pen.setCosmetic(true);  // same width on screen at any zoom
    pen.setWidth(2);
    return pen;
}

} // namespace

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(Qt::black);
}

void ImageView::setImage(const QImage& image) {
    const bool sameSize = image_ != nullptr && image.size() == imageSize();
    if (image_ == nullptr) {
        image_ = scene_->addPixmap(QPixmap::fromImage(image));
        image_->setZValue(-1);  // under the marks
    } else {
        image_->setPixmap(QPixmap::fromImage(image));
    }
    if (!sameSize) {
        clearMarks();
        scene_->setSceneRect(image_->boundingRect());
        fitInView(image_, Qt::KeepAspectRatio);
    }
}

QSize ImageView::imageSize() const {
    return image_ ? image_->pixmap().size() : QSize();
}

void ImageView::addPoint(QPointF at) {
    // A fixed-size ring on screen: ignores the view's zoom.
    auto* ring = scene_->addEllipse(-5, -5, 10, 10, markPen());
    ring->setPos(at);
    ring->setFlag(QGraphicsItem::ItemIgnoresTransformations);
}

void ImageView::addLine(QPointF from, QPointF to) {
    scene_->addLine(QLineF(from, to), markPen());
}

void ImageView::addLabel(QPointF at, const QString& text) {
    auto* label = scene_->addSimpleText(text);
    label->setBrush(QColor(255, 230, 0));
    QFont font = label->font();
    font.setBold(true);
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.4);
    label->setFont(font);
    label->setPos(at);
    label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
}

void ImageView::clearMarks() {
    for (QGraphicsItem* item : scene_->items()) {
        if (item != image_) {
            scene_->removeItem(item);
            delete item;
        }
    }
}

void ImageView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    scale(factor, factor);
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) pressPosition_ = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    const QPoint position = event->position().toPoint();
    // A click, not the end of a drag that panned the view.
    if (event->button() == Qt::LeftButton && image_ != nullptr &&
        (position - pressPosition_).manhattanLength() < 4) {
        const QPointF point = mapToScene(position);
        if (image_->boundingRect().contains(point)) emit clicked(point);
    }
}

} // namespace msapp
