#pragma once

#include "camera_factory.hpp"
#include "capture_station.hpp"

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace msapp {

// Work for the camera thread — a capture, a White reference, a still for
// the mire — run with the open CaptureStation between two preview frames:
// the camera can be opened by one program, one thread, at a time. Shared
// by the window, which posts, and the worker, which runs, so posting never
// touches a worker that may already be gone. A job reports back with
// QMetaObject::invokeMethod; one still queued when the camera stops is
// dropped.
class CameraJobs {
public:
    using Job = std::function<void(CaptureStation&)>;

    void post(Job job) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(job));
    }

    std::vector<Job> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Job> jobs;
        jobs.swap(pending_);
        return jobs;
    }

private:
    std::mutex mutex_;
    std::vector<Job> pending_;
};

// Runs the camera off the GUI thread (issue #20): opening the Moticam A5
// and grabbing a 4K MJPG frame both block, at about 10–15 fps
// (docs/hardware/camera-probe.md), which would freeze the window.
//
// Move it to a QThread and start run() there. It opens a CaptureStation —
// so the camera profile is re-applied and the White reference baked in,
// exactly as a capture would see it (ADR 0014) — then grabs, running any
// posted CameraJobs between frames, until the thread is asked to stop
// (QThread::requestInterruption); then it closes the camera and quits the
// thread.
class PreviewWorker : public QObject {
    Q_OBJECT

public:
    PreviewWorker(CameraConfig config, std::shared_ptr<CameraJobs> jobs);

    // Set when a frame is sent; the GUI thread clears it once it has taken
    // that frame. Until then new frames are dropped rather than queued: 4K
    // frames arrive faster than a busy window may paint them. Shared, so
    // the GUI can clear it even after the worker is gone.
    std::shared_ptr<std::atomic<bool>> framePending() const { return framePending_; }

public slots:
    void run();

signals:
    // The camera is open. `mode` is what it negotiated, e.g. "3840x2160
    // MJPG"; `warnings` names every profile setting it did not grant, and
    // `report` is what CaptureStation::open() said, for the camera setup
    // wizard.
    void opened(const QString& mode, const QStringList& warnings, const msapp::ProfileReport& report);
    // It couldn't open, or stopped delivering frames. The worker is done.
    void failed(const QString& message);
    // A white-balanced frame shrunk to at most 1920x1080 for display, and
    // the size of the frame as grabbed — what a Calibration must match.
    void frameReady(const QImage& frame, const QSize& fullSize);
    // Frames grabbed per second, about once a second.
    void fpsMeasured(double fps);
    // A posted job threw instead of reporting its own error.
    void jobFailed(const QString& message);

private:
    CameraConfig config_;
    std::shared_ptr<CameraJobs> jobs_;
    std::shared_ptr<std::atomic<bool>> framePending_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace msapp

// opened() crosses from the camera thread to the window's.
Q_DECLARE_METATYPE(msapp::ProfileReport)
