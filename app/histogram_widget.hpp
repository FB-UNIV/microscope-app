#pragma once

#include <QWidget>

#include <array>
#include <cstdint>

namespace msapp {

// The Phase analysis histogram (issue #25): the legacy edge-weighted grey
// histogram (AutoThreshold::histogram), with the Seuil as a copper line and
// its tolerance band shaded — the pixels in it count half to each phase.
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    void setHistogram(const std::array<std::uint32_t, 256>& histogram, int threshold, int tolerance);
    void clear();

    QSize sizeHint() const override { return {256, 120}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<std::uint32_t, 256> histogram_{};
    int threshold_ = -1;  // -1: nothing to show
    int tolerance_ = 0;
};

} // namespace msapp
