#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;

namespace msapp {

// A full-size image to point at, with a crosshair cursor for precise clicks.
// The wheel zooms under the mouse; dragging pans (with the middle button
// too); a click (not the end of a drag) reports the point in image pixels —
// sub-pixel when zoomed in. Marks (points, lines, labels) are drawn over the
// image, never into it, and keep their size on screen at any zoom.
//
// Handles are points the operator can correct (#56): dragging one moves it
// instead of panning, and a right-click on one asks to remove it. In
// rectangle mode a left drag draws a rectangle instead of panning, like
// selecting on a desktop. Used to click mire lines and to measure on a Vue.
class ImageView : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    // Shows `image`, fitted to the view when it is the first or a new size;
    // a same-size image (Levels set again) keeps the zoom and the marks.
    void setImage(const QImage& image);
    QSize imageSize() const;

    void addPoint(QPointF at);
    void addLine(QPointF from, QPointF to);
    void addLabel(QPointF at, const QString& text);
    // Replaces the draggable handles with these points, in image pixels.
    // clearMarks() removes them too.
    void setHandles(const std::vector<QPointF>& points);
    void clearMarks();

    // While on, a left drag draws a rectangle (rectangleDragged) instead of
    // panning; the middle button still pans.
    void setRectangleMode(bool on);

signals:
    void clicked(QPointF imagePoint);
    // Handle `index` (its place in setHandles()) is being dragged to
    // `imagePoint`, kept inside the image. Sent on every move.
    void handleMoved(int index, QPointF imagePoint);
    // Handle `index` was right-clicked.
    void handleRemoveRequested(int index);
    // A rectangle drawn in rectangle mode, in image pixels, inside the image.
    void rectangleDragged(QRectF imageRect);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // What the button pressed is doing. A press starts as a Click and turns
    // into a Pan once the mouse moves a few pixels.
    enum class Gesture { None, Click, Pan, Handle, Rectangle };

    // The handle within grabbing distance of `viewPosition`, nearest first.
    std::optional<int> handleAt(QPoint viewPosition) const;
    // `viewPosition` in image pixels, kept inside the image.
    QPointF imagePointAt(QPoint viewPosition) const;
    // Crosshair, or the move cursor over a handle.
    void updateCursor(QPoint viewPosition);

    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_ = nullptr;
    std::vector<QPointF> handles_;
    std::vector<QGraphicsEllipseItem*> handleItems_;
    bool rectangleMode_ = false;

    Gesture gesture_ = Gesture::None;
    Qt::MouseButton gestureButton_ = Qt::NoButton;
    QPoint pressPosition_;
    QPoint lastPanPosition_;
    std::optional<int> dragged_;  // the handle being moved
    QPointF rectangleStart_;
    QGraphicsRectItem* rubberBand_ = nullptr;
};

} // namespace msapp
