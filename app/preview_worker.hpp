#pragma once

#include "camera_factory.hpp"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace msapp {

// Runs the camera off the GUI thread (issue #20): opening the Moticam A5
// and grabbing a 4K MJPG frame both block, at about 10–15 fps
// (docs/hardware/camera-probe.md), which would freeze the window.
//
// Move it to a QThread and start run() there. It opens a CaptureStation —
// so the camera profile is re-applied and the White reference baked in,
// exactly as a capture would see it (ADR 0014) — then grabs until the thread
// is asked to stop (QThread::requestInterruption), closes the camera and
// quits the thread.
class PreviewWorker : public QObject {
    Q_OBJECT

public:
    explicit PreviewWorker(CameraConfig config);

    // Set when a frame is sent; the GUI thread clears it once it has taken
    // that frame. Until then new frames are dropped rather than queued: 4K
    // frames arrive faster than a busy window may paint them. Shared, so
    // the GUI can clear it even after the worker is gone.
    std::shared_ptr<std::atomic<bool>> framePending() const { return framePending_; }

public slots:
    void run();

signals:
    // The camera is open. `mode` is what it negotiated, e.g. "3840x2160
    // MJPG"; `warnings` names every profile setting it did not grant.
    void opened(const QString& mode, const QStringList& warnings);
    // It couldn't open, or stopped delivering frames. The worker is done.
    void failed(const QString& message);
    // A white-balanced frame, shrunk to at most 1920x1080 for display.
    void frameReady(const QImage& frame);
    // Frames grabbed per second, about once a second.
    void fpsMeasured(double fps);

private:
    CameraConfig config_;
    std::shared_ptr<std::atomic<bool>> framePending_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace msapp
