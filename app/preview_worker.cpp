#include "preview_worker.hpp"

#include "capture_station.hpp"
#include "qt_image.hpp"

#include <QElapsedTimer>
#include <QThread>

#include <exception>
#include <utility>

namespace msapp {

namespace {

// Largest frame sent to the window: enough for a full-HD screen, and a 4K
// frame shrunk here costs far less than copying and painting it whole.
const cv::Size kMaxPreviewSize(1920, 1080);

// Between two attempts when the camera sent nothing: a USB hiccup or a
// stream starting up passes within a few of these.
constexpr unsigned long kRetryPauseMs = 50;

QString sizeText(int width, int height) {
    return QStringLiteral("%1x%2").arg(width).arg(height);
}

QString describeMode(const ProfileReport& report) {
    if (report.frameSize.empty()) return QStringLiteral("mode inconnu");
    QString mode = sizeText(report.frameSize.width, report.frameSize.height);
    if (!report.pixelFormat.empty()) mode += QStringLiteral(" ") + QString::fromStdString(report.pixelFormat);
    return mode;
}

// One line per profile setting the camera did not grant (issue #20 banner).
QStringList profileWarnings(const CameraConfig& config, const ProfileReport& report) {
    QStringList warnings;
    if (!report.exposureApplied) {
        warnings << (config.exposureMs
                         ? QStringLiteral("La caméra a refusé l'exposition de %1 ms.").arg(*config.exposureMs)
                         : QStringLiteral("La caméra a refusé l'exposition automatique."));
    }
    if (!report.whiteBalanceApplied) {
        warnings << (config.whiteBalanceKelvin
                         ? QStringLiteral("La caméra a refusé la température de couleur de %1 K : "
                                          "les couleurs peuvent dériver d'une image à l'autre.")
                               .arg(*config.whiteBalanceKelvin)
                         : QStringLiteral("La caméra a refusé la balance des blancs automatique."));
    }
    // ADR 0015: a Calibration only holds at the frame size it was measured at.
    if (!report.frameSizeApplied) {
        const QString asked = sizeText(config.width, config.height);
        warnings << (report.frameSize.empty()
                         ? QStringLiteral("La caméra n'indique pas sa taille d'image (%1 demandé).").arg(asked)
                         : QStringLiteral("La caméra fournit %1 au lieu de %2 : les Calibrations mesurées en %2 "
                                          "ne s'appliquent pas. Reconnectez la caméra pour retrouver ce mode, "
                                          "ou recalibrez.")
                               .arg(sizeText(report.frameSize.width, report.frameSize.height), asked));
    }
    if (!report.pixelFormatApplied) {
        warnings << QStringLiteral("Format d'image %1 au lieu de %2.")
                        .arg(report.pixelFormat.empty() ? QStringLiteral("inconnu")
                                                        : QString::fromStdString(report.pixelFormat),
                             QString::fromStdString(config.pixelFormat));
    }
    return warnings;
}

} // namespace

PreviewWorker::PreviewWorker(CameraConfig config, std::shared_ptr<CameraJobs> jobs)
    : config_(std::move(config)), jobs_(std::move(jobs)) {}

void PreviewWorker::run() {
    QThread* thread = QThread::currentThread();
    try {
        CaptureStation station(config_);
        const ProfileReport report = station.open();
        // Image files have no exposure or frame mode to grant: no warnings.
        const bool replay = config_.backend == "file";
        emit opened(replay ? QStringLiteral("relecture d'images") : describeMode(report),
                    replay ? QStringList() : profileWarnings(config_, report));

        // File replay grabs instantly: pace it like a camera instead of
        // spinning a core.
        QElapsedTimer clock;
        clock.start();
        int grabbed = 0;
        while (!thread->isInterruptionRequested()) {
            for (auto& job : jobs_->take()) {
                try {
                    job(station);
                } catch (const std::exception& e) {
                    emit jobFailed(QString::fromStdString(e.what()));
                }
            }
            // Only this thread sets the flag, so it can't be set between the
            // check and the store below. While the window still holds the
            // last frame, this one is grabbed — the stream keeps moving and
            // the rate stays true — but not shrunk, balanced or copied.
            bool delivered = false;
            if (framePending_->load()) {
                delivered = station.dropFrame();
            } else if (const auto preview = station.grabLivePreview(kMaxPreviewSize)) {
                framePending_->store(true);
                emit frameReady(toQImage(preview->image),
                                QSize(preview->grabbedSize.width, preview->grabbedSize.height));
                delivered = true;
            }
            if (delivered) {
                ++grabbed;
            } else {
                // Warm-up or a dropped frame (review H2): try again shortly.
                // The station throws once too many failed in a row.
                QThread::msleep(kRetryPauseMs);
            }
            if (clock.elapsed() >= 1000) {
                emit fpsMeasured(grabbed * 1000.0 / static_cast<double>(clock.restart()));
                grabbed = 0;
            }
            if (replay) QThread::msleep(100);
        }
        station.close();
    } catch (const std::exception& e) {
        emit failed(QString::fromStdString(e.what()));
    }
    thread->quit();
}

} // namespace msapp
