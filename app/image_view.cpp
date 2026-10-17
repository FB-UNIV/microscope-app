#include "image_view.hpp"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QWheelEvent>

#include <algorithm>

namespace msapp {

namespace {

QPen markPen() {
    QPen pen(QColor(255, 40, 40));
    pen.setCosmetic(true);  // same width on screen at any zoom
    pen.setWidth(2);
    return pen;
}

// How close to a handle, in screen pixels, a press grabs it.
constexpr double kGrabRadius = 9.0;

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

void ImageView::setHandles(const std::vector<QPointF>& points) {
    for (QGraphicsEllipseItem* item : handleItems_) {
        scene_->removeItem(item);
        delete item;
    }
    handleItems_.clear();
    handles_ = points;
    QPen pen(QColor(255, 230, 0));  // yellow: these move, the red marks don't
    pen.setCosmetic(true);
    pen.setWidth(2);
    for (const QPointF& at : handles_) {
        auto* ring = scene_->addEllipse(-6, -6, 12, 12, pen, QBrush(QColor(255, 230, 0, 70)));
        ring->setPos(at);
        ring->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        ring->setToolTip(tr("Glissez pour corriger ce point."));
        handleItems_.push_back(ring);
    }
}

void ImageView::clearMarks() {
    for (QGraphicsItem* item : scene_->items()) {
        if (item != image_) {
            scene_->removeItem(item);
            delete item;
        }
    }
    handleItems_.clear();
    handles_.clear();
}

std::optional<int> ImageView::handleAt(QPoint viewPosition) const {
    std::optional<int> nearest;
    double nearestDistance = kGrabRadius;
    for (std::size_t i = 0; i < handles_.size(); ++i) {
        const QPointF onScreen = mapFromScene(handles_[i]);
        const double distance = QLineF(onScreen, QPointF(viewPosition)).length();
        if (distance <= nearestDistance) {
            nearest = static_cast<int>(i);
            nearestDistance = distance;
        }
    }
    return nearest;
}

void ImageView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    scale(factor, factor);
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressPosition_ = event->position().toPoint();
        // On a handle: move it, without panning the view.
        dragged_ = handleAt(pressPosition_);
        if (dragged_) {
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    if (!dragged_ || image_ == nullptr) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    const QRectF bounds = image_->boundingRect();
    QPointF point = mapToScene(event->position().toPoint());
    point.setX(std::clamp(point.x(), bounds.left(), bounds.right()));
    point.setY(std::clamp(point.y(), bounds.top(), bounds.bottom()));
    const int index = *dragged_;
    if (static_cast<std::size_t>(index) >= handles_.size()) {  // the handles were replaced meanwhile
        dragged_.reset();
        return;
    }
    handles_[static_cast<std::size_t>(index)] = point;
    handleItems_[static_cast<std::size_t>(index)]->setPos(point);
    event->accept();
    emit handleMoved(index, point);  // the receiver may redraw, setHandles() included
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (dragged_ && event->button() == Qt::LeftButton) {
        // The end of a handle's move, not a click.
        dragged_.reset();
        viewport()->setCursor(Qt::OpenHandCursor);  // ScrollHandDrag's own cursor
        event->accept();
        return;
    }
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
