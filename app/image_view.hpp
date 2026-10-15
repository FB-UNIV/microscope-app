#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QPointF>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace msapp {

// A full-size image to point at: the wheel zooms under the mouse, dragging
// pans, and a click (not the end of a drag) reports the point in image
// pixels — sub-pixel when zoomed in. Marks (points, lines, labels) are
// drawn over the image, never into it, and keep their size on screen at
// any zoom. Used to click mire lines and to measure on a Vue.
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
    void clearMarks();

signals:
    void clicked(QPointF imagePoint);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_ = nullptr;
    QPoint pressPosition_;
};

} // namespace msapp
