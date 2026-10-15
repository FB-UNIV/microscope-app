#pragma once

#include "mire_calibration.hpp"

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QStringList>

#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace msapp {

class ImageView;

// Calibration wizard against the 20 µm mire (issue #22): on a full-size
// still of the mire, the operator clicks a line and the line N periods
// further along; each pair is one MireCalibration reading. Wheel zooms,
// dragging pans. Readings repeat until the newest two agree within 0.25 %;
// then the Calibration can be saved, with the still's frame size (ADR 0015).
class MireDialog : public QDialog {
    Q_OBJECT

public:
    // `grossissements` fill the (editable) objective list, `current` picked.
    // The window answers stillRequested() with setStill() or stillFailed().
    MireDialog(const QStringList& grossissements, const QString& current, QWidget* parent = nullptr);

    // A full-size, white-balanced frame of the mire: new readings start.
    void setStill(const QImage& frame);
    void stillFailed(const QString& message);

    // The agreed Calibration; set when the dialog is accepted.
    const std::optional<Calibration>& result() const { return result_; }

signals:
    void stillRequested();

private:
    void restart();  // new readings: new still, new objective
    void click(QPointF point);
    void refreshStatus();

    QComboBox* grossissement_;
    QSpinBox* periods_;
    ImageView* view_;
    QListWidget* readingsList_;
    QLabel* status_;
    QPushButton* save_;

    QSize frameSize_;
    std::optional<MireCalibration> mire_;
    std::optional<QPointF> firstClick_;
    std::optional<Calibration> result_;
};

} // namespace msapp
