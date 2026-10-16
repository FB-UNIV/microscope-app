#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsPixmapItem;
class QGraphicsScene;

namespace msapp {

// A full-size image to point at: the wheel zooms under the mouse, dragging
// pans, and a click (not the end of a drag) reports the point in image
// pixels — sub-pixel when zoomed in. Marks (points, lines, labels) are
// drawn over the image, never into it, and keep their size on screen at
// any zoom. Handles are points the operator can drag to correct a
// measurement (#56): pressing on one moves it instead of panning. Used to
// click mire lines and to measure on a Vue.
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

signals:
    void clicked(QPointF imagePoint);
    // Handle `index` (its place in setHandles()) is being dragged to
    // `imagePoint`, kept inside the image. Sent on every move.
    void handleMoved(int index, QPointF imagePoint);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // The handle within grabbing distance of `viewPosition`, nearest first.
    std::optional<int> handleAt(QPoint viewPosition) const;

    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_ = nullptr;
    QPoint pressPosition_;
    std::vector<QPointF> handles_;
    std::vector<QGraphicsEllipseItem*> handleItems_;
    std::optional<int> dragged_;  // the handle under the mouse since the press
};

} // namespace msapp
