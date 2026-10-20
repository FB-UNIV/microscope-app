#include "image_view.hpp"

#include "theme.hpp"

#include <QBrush>
#include <QFontMetrics>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace msapp {

namespace {

QPen markPen() {
    QPen pen(QColor(255, 40, 40));
    pen.setCosmetic(true);  // same width on screen at any zoom
    pen.setWidth(2);
    return pen;
}

// Yellow: what moves (handles, the guide) and the values, over red lines.
const QColor kYellow(255, 230, 0);

// How close to a handle, in screen pixels, a press grabs it.
constexpr double kGrabRadius = 9.0;
// How far a press may move, in screen pixels, and still be a click.
constexpr int kClickSlop = 4;

// Screen pixels per image pixel: from a 4K photo in a small window to a
// pixel the size of a thumbnail.
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 32.0;

// The loupe: its diameter, and how far from the cursor, in screen pixels.
constexpr int kLoupeSize = 160;
constexpr int kLoupeGap = 24;

QFont labelFont(QFont font) {
    font.setBold(true);
    if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * 1.4);
    return font;
}

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
    viewport()->setMouseTracking(true);  // the cursor changes over a handle; the loupe follows it
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
        zoomToFit();
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
    label->setBrush(kYellow);
    label->setFont(labelFont(label->font()));
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
    QPen pen(kYellow);  // yellow: these move, the red marks don't
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
    guideLine_ = nullptr;
    guideText_ = nullptr;
    guideLabel_.clear();
    // A handle drag survives: the receiver of handleMoved() redraws through
    // here and setHandles() while the drag goes on.
    if (gesture_ == Gesture::Rectangle) gesture_ = Gesture::None;
}

void ImageView::setRectangleMode(bool on) {
    rectangleMode_ = on;
}

double ImageView::zoom() const {
    return transform().m11();
}

void ImageView::scaleWithin(double factor) {
    const double current = zoom();
    const double next = std::clamp(current * factor, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(next, current)) return;
    scale(next / current, next / current);
    emit zoomChanged(zoom());
}

void ImageView::zoomBy(double factor) {
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scaleWithin(factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void ImageView::zoomToFit() {
    if (image_ == nullptr) return;
    fitInView(image_, Qt::KeepAspectRatio);
    fittedTransform_ = transform();
    emit zoomChanged(zoom());
}

void ImageView::zoomToActualSize() {
    if (image_ == nullptr) return;
    const QPointF middle = mapToScene(viewport()->rect().center());
    setTransform(QTransform());
    centerOn(middle);
    emit zoomChanged(zoom());
}

void ImageView::setGuide(std::optional<QLineF> line, const QString& label) {
    if (!line) {
        for (QGraphicsItem* item : {static_cast<QGraphicsItem*>(guideLine_), static_cast<QGraphicsItem*>(guideText_)}) {
            if (item == nullptr) continue;
            scene_->removeItem(item);
            delete item;
        }
        guideLine_ = nullptr;
        guideText_ = nullptr;
        guideLabel_.clear();
        return;
    }
    if (guideLine_ == nullptr) {
        QPen pen(kYellow);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        guideLine_ = scene_->addLine(*line, pen);
    } else {
        guideLine_->setLine(*line);
    }
    guideLabel_ = label;
    if (label.isEmpty()) {
        if (guideText_ != nullptr) guideText_->setText(QString());
        return;
    }
    if (guideText_ == nullptr) {
        guideText_ = scene_->addSimpleText(label);
        guideText_->setBrush(kYellow);
        guideText_->setFont(labelFont(guideText_->font()));
        guideText_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    }
    guideText_->setText(label);
    guideText_->setPos(line->center());
}

void ImageView::setLoupe(bool on) {
    if (loupe_ == on) return;
    const QRect before = loupeRect();
    loupe_ = on;
    viewport()->update(before.united(loupeRect()).adjusted(-3, -3, 3, 3));
}

void ImageView::setPrompt(const QString& text) {
    if (prompt_ == text) return;
    prompt_ = text;
    viewport()->update();
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

void ImageView::cursorMovedTo(QPoint viewPosition) {
    const QRect before = loupeRect();
    cursor_ = viewPosition;
    const QRect after = loupeRect();
    if (before != after) viewport()->update(before.united(after).adjusted(-3, -3, 3, 3));
    const QPointF point = mapToScene(viewPosition);
    const bool over = image_ != nullptr && image_->boundingRect().contains(point);
    if (over) {
        emit hovered(point);
    } else if (overImage_) {
        emit unhovered();
    }
    overImage_ = over;
}

void ImageView::cursorLeft() {
    const QRect before = loupeRect();
    cursor_.reset();
    if (!before.isNull()) viewport()->update(before.adjusted(-3, -3, 3, 3));
    if (overImage_) emit unhovered();
    overImage_ = false;
}

QRect ImageView::loupeRect() const {
    if (!loupe_ || !cursor_ || image_ == nullptr || gesture_ == Gesture::Pan) return {};
    if (!image_->boundingRect().contains(mapToScene(*cursor_))) return {};
    // Above and right of the cursor, out of the way of the hand; flipped
    // near the view's edges.
    QPoint topLeft = *cursor_ + QPoint(kLoupeGap, -kLoupeGap - kLoupeSize);
    if (topLeft.x() + kLoupeSize > viewport()->width()) topLeft.setX(cursor_->x() - kLoupeGap - kLoupeSize);
    if (topLeft.y() < 0) topLeft.setY(cursor_->y() + kLoupeGap);
    return {topLeft, QSize(kLoupeSize, kLoupeSize)};
}

void ImageView::paintLoupe(QPainter& painter, const QRect& where) const {
    // Four times the view's zoom: at fit zoom on a 4K photo, still a few
    // screen pixels per image pixel.
    const double scale = std::clamp(zoom() * 4.0, 3.0, 16.0);
    const QPointF centre = mapToScene(*cursor_);
    const double half = kLoupeSize / 2.0 / scale;
    const QRectF source(centre.x() - half, centre.y() - half, 2 * half, 2 * half);
    const QPixmap& pixmap = image_->pixmap();
    const QRectF shown = source.intersected(QRectF(QPointF(0, 0), QSizeF(pixmap.size())));

    QPainterPath circle;
    circle.addEllipse(QRectF(where));
    painter.save();
    painter.setClipPath(circle);
    painter.fillRect(where, theme::canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);  // the pixels themselves, square
    const QRectF target(where.left() + (shown.left() - source.left()) * scale,
                        where.top() + (shown.top() - source.top()) * scale, shown.width() * scale,
                        shown.height() * scale);
    painter.drawPixmap(target, pixmap, shown);

    // A crosshair with a gap at the point, white on a dark halo.
    const QPointF c = QRectF(where).center();
    const double r = kLoupeSize / 2.0;
    constexpr double gap = 6.0;
    for (const auto& [colour, width] : {std::pair{QColor(0, 0, 0, 150), 3.0}, std::pair{QColor(255, 255, 255, 220), 1.0}}) {
        painter.setPen(QPen(colour, width));
        painter.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() - gap, c.y()));
        painter.drawLine(QPointF(c.x() + gap, c.y()), QPointF(c.x() + r, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() - gap));
        painter.drawLine(QPointF(c.x(), c.y() + gap), QPointF(c.x(), c.y() + r));
    }
    painter.restore();
    painter.setPen(QPen(theme::borderStrong, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(where).adjusted(1, 1, -1, -1));
}

void ImageView::paintPrompt(QPainter& painter) const {
    QFont font = this->font();
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    const int flags = Qt::AlignCenter | Qt::TextWordWrap;
    const int maxWidth = std::max(120, viewport()->width() - 64);
    const QRect text = QFontMetrics(font).boundingRect(QRect(0, 0, maxWidth, 1000), flags, prompt_);
    QRect pill = text.adjusted(-14, -7, 14, 7);
    pill.moveTo((viewport()->width() - pill.width()) / 2, 12);
    QColor edge = theme::accent;
    edge.setAlpha(0x80);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(QColor(0x17, 0x17, 0x19, 230));
    const double radius = std::min(pill.height() / 2.0, 16.0);
    painter.drawRoundedRect(QRectF(pill).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.setPen(theme::text);
    painter.drawText(pill.adjusted(14, 7, -14, -7), flags, prompt_);
}

void ImageView::drawForeground(QPainter* painter, const QRectF& /*rect*/) {
    if (image_ == nullptr) return;
    QPen edge(QColor(255, 255, 255, 20));
    edge.setCosmetic(true);  // one screen pixel at any zoom
    painter->setPen(edge);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(image_->boundingRect());
}

void ImageView::paintEvent(QPaintEvent* event) {
    QGraphicsView::paintEvent(event);
    const QRect loupe = loupeRect();
    if (prompt_.isEmpty() && loupe.isNull()) return;
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    if (!prompt_.isEmpty()) paintPrompt(painter);
    if (!loupe.isNull()) paintLoupe(painter, loupe);
}

void ImageView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    if (loupe_ || !prompt_.isEmpty()) viewport()->update();
}

bool ImageView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::Leave) cursorLeft();
    return QGraphicsView::viewportEvent(event);
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (image_ == nullptr || transform() != fittedTransform_) return;
    zoomToFit();
}

void ImageView::keyPressEvent(QKeyEvent* event) {
    if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        switch (event->key()) {
        case Qt::Key_0:
            zoomToFit();
            event->accept();
            return;
        case Qt::Key_1:
            zoomToActualSize();
            event->accept();
            return;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoomBy(1.25);
            event->accept();
            return;
        case Qt::Key_Minus:
            zoomBy(0.8);
            event->accept();
            return;
        default:
            break;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageView::wheelEvent(QWheelEvent* event) {
    scaleWithin(event->angleDelta().y() > 0 ? 1.25 : 0.8);
    cursorMovedTo(event->position().toPoint());  // the loupe shows the new zoom
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
    cursorMovedTo(position);
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
    if (ended == Gesture::Pan) cursorMovedTo(position);  // the loupe comes back
    updateCursor(position);
    event->accept();
}

} // namespace msapp
