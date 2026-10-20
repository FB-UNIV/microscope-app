#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include <optional>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;

namespace msapp {

// A full-size image to point at, with a crosshair cursor for precise clicks.
// The wheel zooms under the mouse, and the keys 0 (fit), 1 (100 %), + and −
// zoom too; dragging pans (with the middle button too); a click (not the
// end of a drag) reports the point in image pixels — sub-pixel when zoomed
// in. Marks (points, lines, labels) are drawn over the image, never into
// it, and keep their size on screen at any zoom.
//
// Handles are points the operator can correct (#56): dragging one moves it
// instead of panning, and a right-click on one asks to remove it. In
// rectangle mode a left drag draws a rectangle instead of panning, like
// selecting on a desktop. Used to click mire lines and to measure on a Vue.
//
// For placing points precisely (#90): a guide, the dashed line from the
// last point to the cursor with its length; a loupe beside the cursor; and
// a prompt over the top of the image saying what to click next.
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
    // Removes every mark, the handles and the guide.
    void clearMarks();

    // While on, a left drag draws a rectangle (rectangleDragged) instead of
    // panning; the middle button still pans.
    void setRectangleMode(bool on);

    // Screen pixels per image pixel.
    double zoom() const;
    // Zooms about the middle of the view.
    void zoomBy(double factor);
    // The whole image in view; it then follows the window's size until
    // zoomed again.
    void zoomToFit();
    // 100 %: one image pixel per screen pixel, about the middle of the view.
    void zoomToActualSize();

    // The dashed line from the last point to the cursor, `label` at its
    // middle; nothing when empty. In image pixels.
    void setGuide(std::optional<QLineF> line, const QString& label = QString());
    QString guideLabel() const { return guideLabel_; }
    // Beside the cursor, the pixels around it magnified, with a crosshair.
    void setLoupe(bool on);
    // What to do next, over the top of the image; nothing when empty.
    void setPrompt(const QString& text);
    QString prompt() const { return prompt_; }

signals:
    void clicked(QPointF imagePoint);
    // Handle `index` (its place in setHandles()) is being dragged to
    // `imagePoint`, kept inside the image. Sent on every move.
    void handleMoved(int index, QPointF imagePoint);
    // Handle `index` was right-clicked.
    void handleRemoveRequested(int index);
    // A rectangle drawn in rectangle mode, in image pixels, inside the image.
    void rectangleDragged(QRectF imageRect);
    void zoomChanged(double zoom);
    // The cursor over the image, in image pixels, on every move; then
    // unhovered() once it leaves the image or the view.
    void hovered(QPointF imagePoint);
    void unhovered();

protected:
    // A faint neutral edge around the photo, over the canvas.
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    // The scene, then the prompt and the loupe over it.
    void paintEvent(QPaintEvent* event) override;
    // A scroll moves the scene's pixels, overlays included: repaint them.
    void scrollContentsBy(int dx, int dy) override;
    bool viewportEvent(QEvent* event) override;
    // Keeps the whole photo in view as the window resizes, until zoomed.
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
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
    // Scales by `factor` within the zoom limits, about the current anchor.
    void scaleWithin(double factor);
    // Tracks the cursor for the loupe and hovered().
    void cursorMovedTo(QPoint viewPosition);
    void cursorLeft();
    // Where the loupe goes for the cursor at `viewPosition`; empty when it
    // isn't shown.
    QRect loupeRect() const;
    void paintLoupe(QPainter& painter, const QRect& where) const;
    void paintPrompt(QPainter& painter) const;

    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_ = nullptr;
    std::vector<QPointF> handles_;
    std::vector<QGraphicsEllipseItem*> handleItems_;
    bool rectangleMode_ = false;
    // The transform the photo was last fitted with; any other means it was
    // zoomed since, and a resize leaves it alone.
    QTransform fittedTransform_;

    QGraphicsLineItem* guideLine_ = nullptr;
    QGraphicsSimpleTextItem* guideText_ = nullptr;
    QString guideLabel_;
    bool loupe_ = false;
    QString prompt_;
    std::optional<QPoint> cursor_;  // over the viewport, in its pixels
    bool overImage_ = false;        // hovered() sent since the last unhovered()

    Gesture gesture_ = Gesture::None;
    Qt::MouseButton gestureButton_ = Qt::NoButton;
    QPoint pressPosition_;
    QPoint lastPanPosition_;
    std::optional<int> dragged_;  // the handle being moved
    QPointF rectangleStart_;
    QGraphicsRectItem* rubberBand_ = nullptr;
};

} // namespace msapp
