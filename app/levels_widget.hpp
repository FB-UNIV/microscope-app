#pragma once

#include <QWidget>

#include <array>
#include <cstdint>
#include <optional>

namespace msapp {

// Levels on the photo's histogram (#93): every channel value counted
// (levelsHistogram()), with a black and a white handle to drag along it.
// What falls outside the two is shaded: it goes to black or to white.
// The handles stay one level apart at least, as LevelsTreatment requires.
class LevelsWidget : public QWidget {
    Q_OBJECT

public:
    explicit LevelsWidget(QWidget* parent = nullptr);

    void setHistogram(const std::array<std::uint64_t, 256>& histogram);
    // Moves the handles without signalling, e.g. from the spin boxes.
    void setLevels(int black, int white);
    int black() const { return black_; }
    int white() const { return white_; }
    // Where `level` is drawn, in widget pixels: where to press to drag it.
    double xOf(int level) const;

    QSize sizeHint() const override { return {256, 104}; }
    QSize minimumSizeHint() const override { return {160, 80}; }

signals:
    // A handle was dragged; both values, black < white.
    void levelsDragged(int black, int white);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class Handle { Black, White };

    // The plot, inside the frame, above the handles' strip.
    QRectF plot() const;
    int levelAt(double x) const;
    std::optional<Handle> handleNear(double x) const;
    void dragTo(double x);

    std::array<double, 256> heights_{};  // 0..1, square-root scaled
    int black_ = 0;
    int white_ = 255;
    std::optional<Handle> dragged_;
};

} // namespace msapp
