#include "main_window.hpp"

#include "preview_widget.hpp"
#include "preview_worker.hpp"

#include <QAction>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

#include <opencv2/core/utility.hpp>

namespace msapp {

namespace {

// What the profile asks for, shown until the camera says what it granted.
QString requestedMode(const CameraConfig& camera) {
    if (camera.backend == "file") {
        return MainWindow::tr("Relecture de %n image(s)", nullptr, static_cast<int>(camera.frames.size()));
    }
    QString mode = MainWindow::tr("Caméra %1 (%2)")
                       .arg(camera.deviceIndex)
                       .arg(QString::fromStdString(camera.captureApi));
    if (camera.width > 0 && camera.height > 0) {
        mode += MainWindow::tr(", %1x%2 demandé").arg(camera.width).arg(camera.height);
    }
    return mode;
}

} // namespace

MainWindow::MainWindow(CameraConfig camera, QStringList startupWarnings, QWidget* parent)
    : QMainWindow(parent), camera_(std::move(camera)), startupWarnings_(std::move(startupWarnings)) {
    setWindowTitle(QGuiApplication::applicationDisplayName());
    resize(1280, 800);

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setMargin(8);
    banner_->setStyleSheet(QStringLiteral("background: #fff3cd; color: #664d03;"));
    banner_->hide();
    preview_ = new PreviewWidget;

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(banner_);
    layout->addWidget(preview_, 1);
    setCentralWidget(central);

    QToolBar* toolbar = addToolBar(tr("Caméra"));
    toolbar->setMovable(false);
    QAction* reconnect = toolbar->addAction(tr("Reconnecter la caméra"));
    reconnect->setShortcut(QKeySequence::Refresh);
    connect(reconnect, &QAction::triggered, this, &MainWindow::startCamera);

    modeLabel_ = new QLabel;
    fpsLabel_ = new QLabel;
    statusBar()->addWidget(modeLabel_);
    statusBar()->addPermanentWidget(fpsLabel_);
    // The OpenCV the app runs on, as camera_probe reports it. It also makes
    // the exe load the OpenCV DLL at start, so the CI launch check of the
    // Windows bundle proves the bundle ships it.
    statusBar()->addPermanentWidget(
        new QLabel(QStringLiteral("OpenCV %1").arg(QString::fromStdString(cv::getVersionString()))));

    startCamera();
}

MainWindow::~MainWindow() {
    stopCamera();
}

void MainWindow::startCamera() {
    stopCamera();
    const int generation = ++generation_;
    preview_->showMessage(tr("Ouverture de la caméra…"));
    modeLabel_->setText(requestedMode(camera_));
    fpsLabel_->clear();
    showWarnings({});

    thread_ = new QThread(this);
    auto* worker = new PreviewWorker(camera_);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &PreviewWorker::run);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);

    // Queued to this thread. Each lambda checks the generation first: a
    // worker that was stopped may still have signals in the queue.
    connect(worker, &PreviewWorker::opened, this, [this, generation](const QString& mode, const QStringList& warnings) {
        if (generation != generation_) return;
        modeLabel_->setText(tr("Caméra : %1").arg(mode));
        showWarnings(warnings);
    });
    connect(worker, &PreviewWorker::failed, this, [this, generation](const QString& message) {
        if (generation != generation_) return;
        preview_->showMessage(tr("Caméra indisponible\n\n%1\n\nFermez MotiConnect ou tout autre programme qui "
                                 "utilise la caméra, puis cliquez sur « Reconnecter la caméra ».")
                                  .arg(message));
        fpsLabel_->clear();
    });
    // The pending flag, not the worker: it may be deleted before this runs.
    connect(worker, &PreviewWorker::frameReady, this,
            [this, generation, pending = worker->framePending()](const QImage& frame) {
                pending->store(false);
                if (generation != generation_) return;
                preview_->setFrame(frame);
            });
    connect(worker, &PreviewWorker::fpsMeasured, this, [this, generation](double fps) {
        if (generation != generation_) return;
        fpsLabel_->setText(tr("%1 images/s").arg(fps, 0, 'f', 1));
    });

    thread_->start();
}

void MainWindow::stopCamera() {
    if (!thread_) return;
    ++generation_;
    // The worker checks for this between frames, closes the camera and
    // quits; it is deleted as its thread finishes.
    thread_->requestInterruption();
    thread_->quit();
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
}

void MainWindow::showWarnings(const QStringList& cameraWarnings) {
    const QStringList all = startupWarnings_ + cameraWarnings;
    banner_->setText(QStringLiteral("⚠ ") + all.join(QStringLiteral("\n⚠ ")));
    banner_->setVisible(!all.isEmpty());
}

} // namespace msapp
