#include "image_view.hpp"

#include "theme.hpp"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QScrollBar>
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
// How far a press may move, in screen pixels, and still be a click.
constexpr int kClickSlop = 4;

} // namespace

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    // Panning, moving handles and drawing rectangles are handled here, so the
    // cursor stays a crosshair for clicking instead of QGraphicsView's hand.
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(theme::canvas);
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);  // the cursor changes over a handle
    viewport()->setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);  // a click gives it the keyboard, for shortcuts on the image
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
        fittedTransform_ = transform();
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
        ring->setToolTip(tr("Glissez pour corriger ce point, clic droit pour supprimer la mesure."));
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
    rubberBand_ = nullptr;  // deleted with the other marks
    // A handle drag survives: the receiver of handleMoved() redraws through
    // here and setHandles() while the drag goes on.
    if (gesture_ == Gesture::Rectangle) gesture_ = Gesture::None;
}

void ImageView::setRectangleMode(bool on) {
    rectangleMode_ = on;
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

QPointF ImageView::imagePointAt(QPoint viewPosition) const {
    QPointF point = mapToScene(viewPosition);
    if (image_ != nullptr) {
        const QRectF bounds = image_->boundingRect();
        point.setX(std::clamp(point.x(), bounds.left(), bounds.right()));
        point.setY(std::clamp(point.y(), bounds.top(), bounds.bottom()));
    }
    return point;
}

void ImageView::updateCursor(QPoint viewPosition) {
    const Qt::CursorShape shape = handleAt(viewPosition) ? Qt::SizeAllCursor : Qt::CrossCursor;
    if (viewport()->cursor().shape() != shape) viewport()->setCursor(shape);
}

void ImageView::drawForeground(QPainter* painter, const QRectF& /*rect*/) {
    if (image_ == nullptr) return;
    QPen edge(QColor(255, 255, 255, 20));
    edge.setCosmetic(true);  // one screen pixel at any zoom
    painter->setPen(edge);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(image_->boundingRect());
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (image_ == nullptr || transform() != fittedTransform_) return;
    fitInView(image_, Qt::KeepAspectRatio);
    fittedTransform_ = transform();
}

void ImageView::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    scale(factor, factor);
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    const QPoint position = event->position().toPoint();
    if (gesture_ != Gesture::None) {  // a second button while one is held: ignore it
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        if (const auto handle = handleAt(position)) emit handleRemoveRequested(*handle);
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    gestureButton_ = event->button();
    pressPosition_ = position;
    lastPanPosition_ = position;
    if (event->button() == Qt::MiddleButton) {
        gesture_ = Gesture::Pan;
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if (const auto handle = handleAt(position)) {
        gesture_ = Gesture::Handle;
        dragged_ = handle;
    } else if (rectangleMode_ && image_ != nullptr) {
        gesture_ = Gesture::Rectangle;
        rectangleStart_ = imagePointAt(position);
        QPen pen(QColor(0, 220, 255));  // cyan, like the zone's frame on the phase map
        pen.setCosmetic(true);
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        rubberBand_ = scene_->addRect(QRectF(rectangleStart_, rectangleStart_), pen, QBrush(QColor(0, 220, 255, 40)));
    } else {
        gesture_ = Gesture::Click;  // a Pan once it moves
    }
    event->accept();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Two quick clicks are two clicks here (four Vickers corners in a row).
    mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    if (gesture_ == Gesture::Click && (position - pressPosition_).manhattanLength() >= kClickSlop) {
        gesture_ = Gesture::Pan;
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    switch (gesture_) {
    case Gesture::None:
        updateCursor(position);
        QGraphicsView::mouseMoveEvent(event);
        return;
    case Gesture::Click:
        break;
    case Gesture::Pan: {
        const QPoint delta = position - lastPanPosition_;
        lastPanPosition_ = position;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        break;
    }
    case Gesture::Rectangle:
        if (rubberBand_ != nullptr) rubberBand_->setRect(QRectF(rectangleStart_, imagePointAt(position)).normalized());
        break;
    case Gesture::Handle: {
        const int index = dragged_.value_or(-1);
        if (index < 0 || static_cast<std::size_t>(index) >= handles_.size()) {  // replaced meanwhile
            gesture_ = Gesture::None;
            break;
        }
        const QPointF point = imagePointAt(position);
        handles_[static_cast<std::size_t>(index)] = point;
        handleItems_[static_cast<std::size_t>(index)]->setPos(point);
        emit handleMoved(index, point);  // the receiver may redraw, setHandles() included
        break;
    }
    }
    event->accept();
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    if (gesture_ == Gesture::None || event->button() != gestureButton_) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }
    const Gesture ended = gesture_;
    gesture_ = Gesture::None;
    gestureButton_ = Qt::NoButton;
    dragged_.reset();
    if (ended == Gesture::Rectangle && rubberBand_ != nullptr) {
        const QRectF drawn = rubberBand_->rect();
        scene_->removeItem(rubberBand_);
        delete rubberBand_;
        rubberBand_ = nullptr;
        if (drawn.width() >= 2.0 && drawn.height() >= 2.0) {
            emit rectangleDragged(drawn);
        } else if (image_ != nullptr && image_->boundingRect().contains(mapToScene(position))) {
            emit clicked(mapToScene(position));  // barely moved: a click after all
        }
    } else if (ended == Gesture::Click && image_ != nullptr) {
        const QPointF point = mapToScene(position);
        if (image_->boundingRect().contains(point)) emit clicked(point);
    }
    updateCursor(position);
    event->accept();
}

} // namespace msapp
