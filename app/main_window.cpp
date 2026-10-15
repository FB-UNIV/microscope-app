#include "main_window.hpp"

#include "capture_station.hpp"
#include "location_picker.hpp"
#include "mire_dialog.hpp"
#include "preview_widget.hpp"
#include "preview_worker.hpp"
#include "qt_image.hpp"
#include "vue_window.hpp"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <exception>

namespace msapp {

namespace {

constexpr int kPathRole = Qt::UserRole;
constexpr int kThumbnailRole = Qt::UserRole + 1;
const QSize kThumbnailSize(192, 108);

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

// The usual objectives plus every calibrated one, by magnification.
QStringList grossissementChoices(const WorkstationConfig& config) {
    QStringList choices = {QStringLiteral("x4"),  QStringLiteral("x5"),  QStringLiteral("x10"), QStringLiteral("x20"),
                           QStringLiteral("x40"), QStringLiteral("x50"), QStringLiteral("x100")};
    for (const auto& calibration : config.calibrations) {
        const QString name = QString::fromStdString(calibration.grossissement);
        if (!choices.contains(name)) choices << name;
    }
    const auto magnification = [](const QString& name) {
        QString digits;
        for (const QChar c : name) {
            if (c.isDigit() || c == QLatin1Char('.')) digits += c;
        }
        return digits.toDouble();
    };
    std::stable_sort(choices.begin(), choices.end(),
                     [&](const QString& a, const QString& b) { return magnification(a) < magnification(b); });
    return choices;
}

// "15/10/2026 17:43:53" in local time, from the UTC time a VueStore file
// name starts with; the bare name for any other file.
QString vueLabel(const std::filesystem::path& vue, bool principale) {
    const QString name = toQString(vue.filename());
    const QDate date = QDate::fromString(name.left(8), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(name.mid(9, 6), QStringLiteral("HHmmss"));
    QString label = name;
    if (date.isValid() && time.isValid()) {
        label = QDateTime(date, time, QTimeZone(0)).toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"));
    }
    return principale ? QStringLiteral("★ ") + label + MainWindow::tr(" — Vue principale") : label;
}

// A small catalog to start from, written when a teacher opens a catalog
// that doesn't exist yet.
Catalog exampleCatalog() {
    Catalog catalog;
    catalog.departments = {
        {"GMP",
         {{"TP 1: Aciers",
           "Observez chaque acier puis estimez sa teneur en carbone à partir du pourcentage de perlite.",
           {{"Ac1: XC10"}, {"Ac2: XC38"}, {"Ac3: XC80"}}}}},
    };
    return catalog;
}

} // namespace

MainWindow::MainWindow(AppSetup setup, QWidget* parent)
    : QMainWindow(parent), setup_(std::move(setup)), store_(setup_.vueRoot) {
    setWindowTitle(QGuiApplication::applicationDisplayName());
    resize(1400, 860);

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

    // Where the Vue goes.
    picker_ = new LocationPicker;
    QToolBar* where = addToolBar(tr("Échantillon"));
    where->setMovable(false);
    where->addWidget(picker_);
    addToolBarBreak();

    // How it is taken.
    grossissement_ = new QComboBox;
    calibrationLabel_ = new QLabel;
    captureButton_ = new QPushButton(tr("Capturer (Espace)"));
    captureButton_->setShortcut(QKeySequence(Qt::Key_Space));
    captureButton_->setStyleSheet(QStringLiteral("font-weight: bold; padding: 4px 16px;"));
    QToolBar* how = addToolBar(tr("Capture"));
    how->setMovable(false);
    how->addWidget(new QLabel(tr("Grossissement ")));
    how->addWidget(grossissement_);
    how->addWidget(calibrationLabel_);
    how->addSeparator();
    how->addWidget(captureButton_);
    how->addSeparator();
    QAction* reconnect = how->addAction(tr("Reconnecter la caméra"));
    reconnect->setShortcut(QKeySequence::Refresh);

    // The TP and the Échantillon's Vues.
    instructions_ = new QLabel;
    instructions_->setWordWrap(true);
    instructions_->setStyleSheet(QStringLiteral("background: #e7f1ff; padding: 6px;"));
    vuesTitle_ = new QLabel;
    vueList_ = new QListWidget;
    vueList_->setIconSize(kThumbnailSize);
    vueList_->setToolTip(tr("Double-cliquez une Vue pour l'ouvrir : traitements, mesures, analyse de phases."));
    auto* open = new QPushButton(tr("Ouvrir la Vue"));
    auto* principale = new QPushButton(tr("★ Choisir comme Vue principale"));
    principale->setToolTip(tr("La Vue principale de l'Échantillon est celle de la Fiche du TP."));
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->addWidget(instructions_);
    sideLayout->addWidget(vuesTitle_);
    sideLayout->addWidget(vueList_, 1);
    auto* vueButtons = new QHBoxLayout;
    vueButtons->addWidget(open);
    vueButtons->addWidget(principale);
    sideLayout->addLayout(vueButtons);
    auto* dock = new QDockWidget(tr("TP et Vues"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(side);
    dock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    modeLabel_ = new QLabel;
    fpsLabel_ = new QLabel;
    statusBar()->addWidget(modeLabel_);
    statusBar()->addPermanentWidget(fpsLabel_);
    // The OpenCV the app runs on, as camera_probe reports it. It also makes
    // the exe load the OpenCV DLL at start, so the CI launch check of the
    // Windows bundle proves the bundle ships it.
    statusBar()->addPermanentWidget(
        new QLabel(QStringLiteral("OpenCV %1").arg(QString::fromStdString(cv::getVersionString()))));

    buildMenus();
    fillGrossissements();
    picker_->setCatalog(setup_.catalog);

    connect(reconnect, &QAction::triggered, this, &MainWindow::startCamera);
    connect(captureButton_, &QPushButton::clicked, this, &MainWindow::capture);
    connect(principale, &QPushButton::clicked, this, &MainWindow::flagPrincipale);
    connect(open, &QPushButton::clicked, this, &MainWindow::openVue);
    connect(vueList_, &QListWidget::itemActivated, this, &MainWindow::openVue);
    connect(grossissement_, &QComboBox::currentTextChanged, this, &MainWindow::refreshCaptureState);
    connect(picker_, &LocationPicker::locationChanged, this, [this] {
        refreshVues();
        refreshCaptureState();
    });

    refreshVues();
    refreshCaptureState();
    startCamera();
}

MainWindow::~MainWindow() {
    stopCamera();
}

void MainWindow::buildMenus() {
    QMenu* poste = menuBar()->addMenu(tr("&Poste"));
    connect(poste->addAction(tr("Mesurer la référence blanche…")), &QAction::triggered, this,
            &MainWindow::measureWhiteReference);
    connect(poste->addAction(tr("Calibrer un grossissement sur la mire…")), &QAction::triggered, this,
            &MainWindow::calibrateGrossissement);
    poste->addSeparator();
    connect(poste->addAction(tr("Modifier le catalogue de TP…")), &QAction::triggered, this,
            &MainWindow::openCatalogFile);
    connect(poste->addAction(tr("Recharger le catalogue de TP")), &QAction::triggered, this,
            &MainWindow::reloadCatalog);
    poste->addSeparator();
    connect(poste->addAction(tr("Ouvrir le dossier des Vues")), &QAction::triggered, this,
            &MainWindow::openVueFolder);
}

void MainWindow::fillGrossissements() {
    const QString current = grossissement_->currentText();
    grossissement_->clear();
    grossissement_->addItems(grossissementChoices(setup_.config));
    const int index = grossissement_->findText(current);
    grossissement_->setCurrentIndex(index >= 0 ? index : grossissement_->findText(QStringLiteral("x10")));
}

std::optional<VueLocation> MainWindow::location() const {
    return picker_->location();
}

QString MainWindow::grossissement() const {
    return grossissement_->currentText();
}

void MainWindow::startCamera() {
    stopCamera();
    const int generation = ++generation_;
    preview_->showMessage(tr("Ouverture de la caméra…"));
    modeLabel_->setText(requestedMode(setup_.camera));
    fpsLabel_->clear();
    cameraWarnings_.clear();

    jobs_ = std::make_shared<CameraJobs>();
    thread_ = new QThread(this);
    auto* worker = new PreviewWorker(setup_.camera, jobs_);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &PreviewWorker::run);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);

    // Queued to this thread. Each lambda checks the generation first: a
    // worker that was stopped may still have signals in the queue.
    connect(worker, &PreviewWorker::opened, this, [this, generation](const QString& mode, const QStringList& warnings) {
        if (generation != generation_) return;
        cameraOpen_ = true;
        modeLabel_->setText(tr("Caméra : %1").arg(mode));
        cameraWarnings_ = warnings;
        refreshCaptureState();
    });
    connect(worker, &PreviewWorker::failed, this, [this, generation](const QString& message) {
        if (generation != generation_) return;
        cameraOpen_ = false;
        busy_ = false;
        preview_->showMessage(tr("Caméra indisponible\n\n%1\n\nFermez MotiConnect ou tout autre programme qui "
                                 "utilise la caméra, puis cliquez sur « Reconnecter la caméra ».")
                                  .arg(message));
        fpsLabel_->clear();
        refreshCaptureState();
    });
    // The pending flag, not the worker: it may be deleted before this runs.
    connect(worker, &PreviewWorker::frameReady, this,
            [this, generation, pending = worker->framePending()](const QImage& frame, const QSize& fullSize) {
                pending->store(false);
                if (generation != generation_) return;
                preview_->setFrame(frame);
                if (fullSize != frameSize_) {
                    frameSize_ = fullSize;
                    refreshCaptureState();
                }
            });
    connect(worker, &PreviewWorker::fpsMeasured, this, [this, generation](double fps) {
        if (generation != generation_) return;
        fpsLabel_->setText(tr("%1 images/s").arg(fps, 0, 'f', 1));
    });
    connect(worker, &PreviewWorker::jobFailed, this, [this](const QString& message) {
        QMessageBox::warning(this, tr("Erreur de la caméra"), message);
    });

    thread_->start();
    refreshCaptureState();
}

void MainWindow::stopCamera() {
    if (!thread_) return;
    ++generation_;
    // The worker checks for this between frames, closes the camera and
    // quits; it is deleted as its thread finishes. Jobs still queued are
    // dropped with their queue.
    thread_->requestInterruption();
    thread_->quit();
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
    jobs_.reset();
    cameraOpen_ = false;
    busy_ = false;
    frameSize_ = QSize();
}

void MainWindow::refreshBanner() {
    QStringList all = setup_.warnings;
    if (!catalogWarning_.isEmpty()) all << catalogWarning_;
    all << cameraWarnings_;
    if (!captureWarning_.isEmpty()) all << captureWarning_;
    banner_->setText(QStringLiteral("⚠ ") + all.join(QStringLiteral("\n⚠ ")));
    banner_->setVisible(!all.isEmpty());
}

void MainWindow::refreshCaptureState() {
    const QString name = grossissement();
    captureWarning_.clear();
    const auto stored = setup_.config.calibration(name.toStdString());
    if (!stored) {
        calibrationLabel_->setText(tr(" non calibré "));
        captureWarning_ = tr("%1 n'est pas calibré sur ce poste : les Vues seront prises sans Échelle, et sans "
                             "mesure possible. Menu Poste › Calibrer un grossissement.")
                              .arg(name);
    } else {
        calibrationLabel_->setText(tr(" %1 px/µm, image %2x%3 ")
                                       .arg(stored->pixelsPerMicron, 0, 'f', 4)
                                       .arg(stored->frameWidth)
                                       .arg(stored->frameHeight));
        if (frameSize_.isValid() && !stored->fitsFrame(frameSize_.width(), frameSize_.height())) {
            // ADR 0015: never applied to another frame size, never rescaled.
            captureWarning_ = tr("%1 a été calibré en %2x%3 mais la caméra fournit %4x%5 : les Vues seront prises "
                                 "sans Calibration. Reconnectez la caméra pour retrouver ce mode, ou recalibrez %1.")
                                  .arg(name)
                                  .arg(stored->frameWidth)
                                  .arg(stored->frameHeight)
                                  .arg(frameSize_.width())
                                  .arg(frameSize_.height());
        }
    }
    captureButton_->setEnabled(cameraOpen_ && !busy_ && location().has_value());
    refreshBanner();
}

void MainWindow::refreshVues() {
    vueList_->clear();
    const Tp* tp = picker_->tp();
    instructions_->setText(tp ? QString::fromStdString(tp->instructions) : QString());
    instructions_->setVisible(!instructions_->text().isEmpty());

    const auto where = location();
    if (!where) {
        vuesTitle_->setText(tr("Ce TP n'a pas d'Échantillon : complétez le catalogue."));
        return;
    }
    std::vector<std::filesystem::path> vues;
    std::optional<std::filesystem::path> principale;
    try {
        vues = store_.listVues(*where);
        principale = store_.principale(*where);
    } catch (const std::exception& e) {
        vuesTitle_->setText(QString::fromStdString(e.what()));
        return;
    }
    vuesTitle_->setText(where->isUnsorted() ? tr("Vues non classées : %n", nullptr, static_cast<int>(vues.size()))
                                            : tr("Vues de l'Échantillon : %n", nullptr, static_cast<int>(vues.size())));
    for (auto it = vues.rbegin(); it != vues.rend(); ++it) {  // newest first
        auto* item = new QListWidgetItem(vueLabel(*it, principale && *principale == *it), vueList_);
        item->setData(kPathRole, toQString(*it));
        item->setToolTip(toQString(it->filename()));
    }
    QTimer::singleShot(0, this, &MainWindow::loadNextThumbnail);
}

void MainWindow::loadNextThumbnail() {
    // One per event-loop turn, so the preview keeps running meanwhile.
    for (int i = 0; i < vueList_->count(); ++i) {
        QListWidgetItem* item = vueList_->item(i);
        if (item->data(kThumbnailRole).toBool()) continue;
        item->setData(kThumbnailRole, true);
        const cv::Mat image = cv::imread(toPath(item->data(kPathRole).toString()).string(), cv::IMREAD_REDUCED_COLOR_4);
        if (!image.empty()) {
            item->setIcon(QIcon(QPixmap::fromImage(toQImage(image, kThumbnailSize))));
        }
        QTimer::singleShot(0, this, &MainWindow::loadNextThumbnail);
        return;
    }
}

void MainWindow::capture() {
    const auto where = location();
    if (!cameraOpen_ || busy_ || !where || !jobs_) return;
    std::optional<Calibration> calibration;
    if (frameSize_.isValid()) {
        try {
            calibration =
                setup_.config.calibrationForFrame(grossissement().toStdString(), frameSize_.width(), frameSize_.height());
        } catch (const std::exception&) {
            calibration.reset();  // measured at another frame size: the banner says so
        }
    }
    std::filesystem::path path;
    try {
        path = store_.newVuePath(*where);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Capture impossible"), QString::fromStdString(e.what()));
        return;
    }

    busy_ = true;
    refreshCaptureState();
    statusBar()->showMessage(tr("Capture…"));
    const int generation = generation_;
    jobs_->post([this, generation, path, calibration](CaptureStation& station) {
        QString error;
        try {
            station.capture(path, calibration);
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            this, [this, generation, path, error] { captured(generation, path, error); }, Qt::QueuedConnection);
    });
}

void MainWindow::captured(int generation, const std::filesystem::path& vue, const QString& error) {
    if (generation == generation_) {
        busy_ = false;
        refreshCaptureState();
    }
    if (!error.isEmpty()) {
        statusBar()->clearMessage();
        QMessageBox::warning(this, tr("Capture refusée"), error);
        return;
    }
    statusBar()->showMessage(tr("Vue enregistrée : %1").arg(toQString(vue)), 10000);
    refreshVues();
}

void MainWindow::flagPrincipale() {
    const QListWidgetItem* item = vueList_->currentItem();
    const auto where = location();
    if (item == nullptr || !where) {
        QMessageBox::information(this, tr("Vue principale"), tr("Sélectionnez d'abord une Vue dans la liste."));
        return;
    }
    try {
        store_.setPrincipale(*where, toPath(item->data(kPathRole).toString()));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Vue principale"), QString::fromStdString(e.what()));
        return;
    }
    refreshVues();
}

void MainWindow::openVue() {
    const QListWidgetItem* item = vueList_->currentItem();
    const auto where = location();
    if (item == nullptr || !where) return;
    try {
        Vue vue = Vue::load(toPath(item->data(kPathRole).toString()));
        auto* window = new VueWindow(std::move(vue), store_, *where, this);  // a window of its own
        connect(window, &VueWindow::principaleChanged, this, &MainWindow::refreshVues);
        window->show();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Vue illisible"), QString::fromStdString(e.what()));
    }
}

void MainWindow::measureWhiteReference() {
    if (!cameraOpen_ || !jobs_) {
        QMessageBox::information(this, tr("Référence blanche"), tr("La caméra n'est pas disponible."));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Référence blanche"),
        tr("Retirez l'échantillon, allumez la lampe et regardez un champ vide et uniforme.\n\n"
           "Mesurer maintenant la référence blanche de ce microscope ?"));
    if (answer != QMessageBox::Yes) return;

    const int generation = generation_;
    jobs_->post([this, generation](CaptureStation& station) {
        std::optional<WhiteBalance> gains;
        QString error;
        try {
            gains = station.calibrateWhiteReference();  // applied to the preview straight away
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, gains, error] {
                if (!error.isEmpty()) {
                    QMessageBox::warning(this, tr("Champ refusé"), error);
                    return;
                }
                if (generation != generation_ || !gains) return;  // that camera is gone: measure again
                setup_.config.camera.whiteReference = *gains;
                setup_.camera.whiteReference = *gains;
                if (saveConfig()) {
                    QMessageBox::information(this, tr("Référence blanche"),
                                             tr("Référence blanche enregistrée : rouge %1, vert %2, bleu %3.")
                                                 .arg(gains->red, 0, 'f', 3)
                                                 .arg(gains->green, 0, 'f', 3)
                                                 .arg(gains->blue, 0, 'f', 3));
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::calibrateGrossissement() {
    if (!cameraOpen_ || !jobs_) {
        QMessageBox::information(this, tr("Calibration"), tr("La caméra n'est pas disponible."));
        return;
    }
    auto* dialog = new MireDialog(grossissementChoices(setup_.config), grossissement(), this);
    const QPointer<MireDialog> guard(dialog);
    connect(dialog, &MireDialog::stillRequested, this, [this, guard] { requestStill(guard); });
    requestStill(guard);
    std::optional<Calibration> result;
    if (dialog->exec() == QDialog::Accepted) result = dialog->result();
    delete dialog;
    if (!result) return;

    if (const auto existing = setup_.config.calibration(result->grossissement)) {
        const auto answer = QMessageBox::question(
            this, tr("Calibration"),
            tr("Remplacer la Calibration de %1 (%2 px/µm, image %3x%4) ? Les Vues déjà prises gardent la leur.")
                .arg(QString::fromStdString(existing->grossissement))
                .arg(existing->pixelsPerMicron, 0, 'f', 4)
                .arg(existing->frameWidth)
                .arg(existing->frameHeight));
        if (answer != QMessageBox::Yes) return;
    }
    setup_.config.setCalibration(*result);
    if (saveConfig()) {
        statusBar()->showMessage(tr("Calibration de %1 enregistrée.").arg(QString::fromStdString(result->grossissement)),
                                 10000);
    }
    fillGrossissements();
    grossissement_->setCurrentText(QString::fromStdString(result->grossissement));
    refreshCaptureState();
}

void MainWindow::requestStill(QPointer<MireDialog> dialog) {
    if (!cameraOpen_ || !jobs_) {
        if (dialog) dialog->stillFailed(tr("la caméra n'est pas disponible."));
        return;
    }
    jobs_->post([this, dialog](CaptureStation& station) {
        QImage still;
        QString error;
        try {
            still = toQImage(station.grabPreview());  // full size: the clicks are in its pixels
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [dialog, still, error] {
                if (!dialog) return;
                if (error.isEmpty()) {
                    dialog->setStill(still);
                } else {
                    dialog->stillFailed(error);
                }
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::openCatalogFile() {
    if (setup_.catalogPath.empty()) {
        QMessageBox::warning(this, tr("Catalogue de TP"), tr("Ce poste n'a pas de dossier de configuration."));
        return;
    }
    if (!std::filesystem::exists(setup_.catalogPath)) {
        const auto answer = QMessageBox::question(
            this, tr("Catalogue de TP"),
            tr("Ce poste n'a pas encore de catalogue de TP. En créer un d'exemple, à modifier ?"));
        if (answer != QMessageBox::Yes) return;
        try {
            saveCatalog(exampleCatalog(), setup_.catalogPath);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Catalogue de TP"), QString::fromStdString(e.what()));
            return;
        }
    }
    const QString path = toQString(setup_.catalogPath);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::information(this, tr("Catalogue de TP"),
                                 tr("Ouvrez ce fichier dans un éditeur de texte (UTF-8) :\n%1").arg(path));
        return;
    }
    QMessageBox::information(this, tr("Catalogue de TP"),
                             tr("Une fois le fichier enregistré, choisissez Poste › Recharger le catalogue de TP."));
}

void MainWindow::reloadCatalog() {
    if (setup_.catalogPath.empty()) return;
    const LoadedCatalog loaded = loadCatalog(setup_.catalogPath);
    if (!loaded.error.empty()) {
        // Keep the catalog in use: a typo must not send every Vue to Unsorted.
        catalogWarning_ = tr("Catalogue de TP illisible, l'ancien reste utilisé : %1")
                              .arg(QString::fromStdString(loaded.error));
        refreshBanner();
        return;
    }
    catalogWarning_.clear();
    setup_.catalog = loaded.catalog;
    picker_->setCatalog(setup_.catalog);
    statusBar()->showMessage(tr("Catalogue de TP rechargé."), 5000);
}

void MainWindow::openVueFolder() {
    std::filesystem::path folder = store_.root();
    if (const auto where = location()) {
        try {
            if (std::filesystem::is_directory(store_.folder(*where))) folder = store_.folder(*where);
        } catch (const std::exception&) {
        }
    }
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(folder)));
}

bool MainWindow::saveConfig() {
    if (setup_.configPath.empty()) {
        QMessageBox::warning(this, tr("Configuration du poste"),
                             tr("Ce poste n'a pas de dossier de configuration : le réglage ne sera pas gardé."));
        return false;
    }
    try {
        saveWorkstationConfig(setup_.config, setup_.configPath);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Configuration du poste"),
                              tr("Enregistrement impossible : %1").arg(QString::fromStdString(e.what())));
        return false;
    }
}

} // namespace msapp
