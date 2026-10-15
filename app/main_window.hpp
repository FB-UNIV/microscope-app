#pragma once

#include "camera_factory.hpp"

#include <QMainWindow>
#include <QStringList>

class QLabel;
class QThread;

namespace msapp {

class PreviewWidget;

// The capture app's main window: a live preview of the workstation's
// camera, run on a worker thread (issue #20), with a banner for every
// profile setting the camera did not grant (ADR 0014, ADR 0015). The
// capture button comes once Vues have unique paths (#18).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // `camera` is the profile to preview with. `startupWarnings` stay in the
    // banner above any camera warning, e.g. an unreadable config file.
    explicit MainWindow(CameraConfig camera, QStringList startupWarnings = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    // (Re)opens the camera on a fresh worker thread.
    void startCamera();
    // Asks the worker to stop and waits for it to close the camera.
    void stopCamera();
    void showWarnings(const QStringList& cameraWarnings);

    CameraConfig camera_;
    QStringList startupWarnings_;
    PreviewWidget* preview_ = nullptr;
    QLabel* banner_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QThread* thread_ = nullptr;
    // Bumped whenever the camera restarts or stops: signals a previous
    // worker had already queued are ignored instead of shown.
    int generation_ = 0;
};

} // namespace msapp
