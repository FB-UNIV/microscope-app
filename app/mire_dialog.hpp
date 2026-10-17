#pragma once

#include "mire_calibration.hpp"

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QStringList>

#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace msapp {

class ImageView;

// Calibration wizard against the mire (issue #22): on a full-size still of
// the mire, the operator clicks a line and the line N periods (gaps, not
// lines) further along; each pair is one MireCalibration reading. Only the
// distance across the lines counts, in the direction found in the still,
// so the two clicks need not be level (#65). Wheel zooms, dragging pans.
// Readings repeat until the newest two agree within 0.25 %; then the
// Calibration can be saved, with the still's frame size (ADR 0015).
class MireDialog : public QDialog {
    Q_OBJECT

public:
    // `grossissements` fill the (editable) objective list, `current` picked;
    // `pitchMicrons` is the mire's period, 20 µm for the lab's (#65).
    // The window answers stillRequested() with setStill() or stillFailed().
    MireDialog(const QStringList& grossissements, const QString& current, double pitchMicrons,
               QWidget* parent = nullptr);

    // A full-size, white-balanced frame of the mire, with the direction
    // across its lines (mireLineNormal; empty when none was found): new
    // readings start.
    void setStill(const QImage& frame, std::optional<cv::Vec2d> lineNormal);
    void stillFailed(const QString& message);

    // The agreed Calibration; set when the dialog is accepted.
    const std::optional<Calibration>& result() const { return result_; }
    // The mire's period as last set in the dialog.
    double pitchMicrons() const;

signals:
    void stillRequested();

private:
    void restart();  // new readings: new still, new objective
    void click(QPointF point);
    void refreshStatus();

    QComboBox* grossissement_;
    QDoubleSpinBox* pitch_;
    QSpinBox* periods_;
    ImageView* view_;
    QListWidget* readingsList_;
    QLabel* direction_;  // whether the lines' direction was found in the still
    QLabel* status_;
    QPushButton* save_;

    QSize frameSize_;
    std::optional<cv::Vec2d> lineNormal_;
    std::optional<MireCalibration> mire_;
    std::optional<QPointF> firstClick_;
    std::optional<Calibration> result_;
};

} // namespace msapp
