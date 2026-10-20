#pragma once

#include "camera_setup.hpp"
#include "workstation_config.hpp"

#include <QDialog>
#include <QImage>
#include <QString>

#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

namespace msapp {

class PreviewWidget;

// Poste › Configurer la caméra (issue #83): sets up a workstation's Camera
// profile without editing workstation.json, the live preview on screen at
// every step so each setting's effect shows. Steps: the camera (the ones
// findCameras() lists), its mode (frame size, pixel format, capture API —
// and what the camera actually granted), exposure, colour temperature, the
// White reference (ADR 0014), then a summary of what saving means: settings
// the camera refused, Calibrations that stop applying in the new mode (ADR
// 0015), a White reference to measure again.
//
// The dialog never touches the camera itself: the window runs it and
// answers each request — searchRequested() with camerasFound(),
// profileRequested() by reopening the camera with that profile and then
// cameraOpened() or cameraFailed(), controlsRequested() with
// controlsApplied(), whiteReferenceRequested() with whiteReferenceMeasured()
// or whiteReferenceFailed() — and feeds it frames. Accepted, review() is what
// to save.
class CameraSetupDialog : public QDialog {
    Q_OBJECT

public:
    // `saved` is the workstation as saved, `start` the profile the camera
    // runs with now, `captureApis` those worth trying (platformCaptureApis).
    CameraSetupDialog(WorkstationConfig saved, CameraConfig start, std::vector<std::string> captureApis,
                      QWidget* parent = nullptr);

    // The window's answers.
    void showMessage(const QString& message);  // replaces the preview, with a spinner, while searching
    void cameraStarting();
    void cameraOpened(const ProfileReport& report);
    void cameraFailed(const QString& message);
    void setFrame(const QImage& frame);
    void setFps(double fps);
    void camerasFound(const std::vector<FoundCamera>& cameras, const QString& error);
    void controlsApplied(const ProfileReport& report);
    void whiteReferenceMeasured(const WhiteBalance& gains);
    void whiteReferenceFailed(const QString& message);

    // The profile being tried, with every change made so far.
    const CameraConfig& proposed() const { return proposed_; }
    // What saving it means; only meaningful once the camera opened with it.
    CameraSetupReview review() const;
    // Accepted with "calibrate on the mire next" ticked.
    bool recalibrateRequested() const;

signals:
    void searchRequested();
    // Reopen the camera with this profile (a new camera or mode).
    void profileRequested(const msapp::CameraConfig& profile);
    // Apply this profile's exposure and colour temperature to the open
    // camera, without reopening it.
    void controlsRequested(const msapp::CameraConfig& profile);
    void whiteReferenceRequested();

private:
    enum Page { CameraPage, ModePage, ExposurePage, ColourPage, WhitePage, SavePage, PageCount };

    void goTo(int page);
    // Every change goes through here. `reopen`: the camera must reopen (a
    // new camera or mode); otherwise only its controls change.
    void propose(CameraConfig next, bool reopen);
    void restoreA5();
    // The widgets from proposed_, without their change signals.
    void showProposal();
    // Every label that depends on what the camera granted.
    void refreshStatus();
    QString summaryHtml(const CameraSetupReview& review) const;

    WorkstationConfig saved_;
    CameraConfig proposed_;
    std::vector<std::string> captureApis_;
    std::vector<FoundCamera> cameras_;
    // What the camera granted proposed_; empty while it (re)opens or after it failed.
    std::optional<ProfileReport> report_;
    QString failure_;
    double fps_ = 0.0;
    bool searching_ = false;
    bool measuringWhite_ = false;
    // proposed_.whiteReference was measured in this dialog, with its settings.
    bool whiteMeasured_ = false;

    PreviewWidget* preview_;
    QLabel* liveStatus_;
    QWidget* stepStrip_;  // a StepStrip, local to the .cpp
    QLabel* stepTitle_;
    QStackedWidget* pages_;
    QPushButton* back_;
    QPushButton* next_;
    QPushButton* save_;

    QListWidget* cameraList_;
    QPushButton* search_;
    QLabel* searchStatus_;

    QComboBox* api_;
    QComboBox* mode_;
    QLabel* granted_;
    QLabel* calibrations_;

    QRadioButton* autoExposure_;
    QRadioButton* fixedExposure_;
    QComboBox* exposure_;
    QLabel* exposureStatus_;

    QRadioButton* autoColour_;
    QRadioButton* lockedColour_;
    QSpinBox* kelvin_;
    QLabel* colourStatus_;

    QLabel* whiteStatus_;
    QPushButton* measureWhite_;

    QLabel* summary_;
    QCheckBox* recalibrate_;
};

} // namespace msapp
