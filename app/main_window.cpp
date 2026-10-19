#include "main_window.hpp"

#include "capture_station.hpp"
#include "fiche.hpp"
#include "fiche_pdf.hpp"
#include "location_picker.hpp"
#include "mire_dialog.hpp"
#include "preview_widget.hpp"
#include "preview_worker.hpp"
#include "qt_image.hpp"
#include "vue_window.hpp"
#include "wait_cursor.hpp"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
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
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QThread>
#include <QTimeZone>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVariant>
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

bool hasEchantillon(const Catalog& catalog) {
    for (const auto& department : catalog.departments) {
        for (const auto& tp : department.tps) {
            if (!tp.echantillons.empty()) return true;
        }
    }
    return false;
}

// Asks for the Échantillon to file `vue` under, starting from `start`;
// empty when cancelled.
std::optional<VueLocation> pickEchantillon(QWidget* parent, const Catalog& catalog,
                                           const std::optional<VueLocation>& start, const std::filesystem::path& vue,
                                           const QIcon& thumbnail) {
    QDialog dialog(parent);
    dialog.setWindowTitle(MainWindow::tr("Classer la Vue"));
    auto* photo = new QLabel;
    if (!thumbnail.isNull()) photo->setPixmap(thumbnail.pixmap(kThumbnailSize));
    auto* picker = new LocationPicker;
    picker->setCatalog(catalog);
    if (start && !start->isUnsorted()) picker->setLocation(*start);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(MainWindow::tr("Classer"));

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(MainWindow::tr("Vue : %1").arg(vueLabel(vue, false))));
    layout->addWidget(photo);
    layout->addWidget(new QLabel(MainWindow::tr("La ranger dans l'Échantillon :")));
    layout->addWidget(picker);
    layout->addWidget(buttons);

    const auto refresh = [picker, ok] {
        const auto to = picker->location();
        ok->setEnabled(to && !to->isUnsorted());
    };
    QObject::connect(picker, &LocationPicker::locationChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return picker->location();
}

// This PC's window preferences, such as a hidden side panel, in ui.ini
// beside workstation.json rather than in it: they are the operator's
// choice, not part of the camera or microscope setup. Nothing is kept when
// there is no config folder.
const QString kPanelVisibleKey = QStringLiteral("mainWindow/panelVisible");

QVariant uiSetting(const std::filesystem::path& configPath, const QString& key, const QVariant& fallback) {
    if (configPath.empty()) return fallback;
    return QSettings(toQString(configPath.parent_path() / "ui.ini"), QSettings::IniFormat).value(key, fallback);
}

void setUiSetting(const std::filesystem::path& configPath, const QString& key, const QVariant& value) {
    if (configPath.empty()) return;
    QSettings(toQString(configPath.parent_path() / "ui.ini"), QSettings::IniFormat).setValue(key, value);
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
    // The build in the title bar, so a screenshot from a lab PC says which
    // build it came from.
    const QString version = QCoreApplication::applicationVersion();
    setWindowTitle(version.isEmpty()
                       ? QGuiApplication::applicationDisplayName()
                       : QStringLiteral("%1 — %2").arg(QGuiApplication::applicationDisplayName(), version));
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
    unsortedButton_ = new QPushButton;
    unsortedButton_->setCheckable(true);
    unsortedButton_->setToolTip(tr("Les Vues prises avant que ce poste ait un catalogue de TP, à ranger chacune dans "
                                   "son Échantillon."));
    vuesTitle_ = new QLabel;
    vuesTitle_->setWordWrap(true);
    vueList_ = new QListWidget;
    vueList_->setIconSize(kThumbnailSize);
    vueList_->setToolTip(tr("Double-cliquez une Vue pour l'ouvrir : traitements, mesures, analyse de phases."));
    auto* open = new QPushButton(tr("Ouvrir la Vue"));
    auto* principale = new QPushButton(tr("★ Choisir comme Vue principale"));
    principale->setToolTip(tr("La Vue principale de l'Échantillon est celle de la Fiche du TP."));
    auto* file = new QPushButton(tr("Classer la Vue…"));
    file->setToolTip(tr("Range la Vue choisie, avec ses traitements, dans un Échantillon du catalogue : une Vue non "
                        "classée, ou prise sous le mauvais Échantillon."));
    auto* remove = new QPushButton(tr("Supprimer la Vue…"));
    remove->setToolTip(tr("Déplace la Vue choisie, avec ses traitements, dans la Corbeille du dossier des Vues "
                          "(touche Suppr). Rien n'est effacé."));
    // The same on the list itself: right-click, and Suppr.
    vueList_->setContextMenuPolicy(Qt::ActionsContextMenu);
    QAction* openAction = new QAction(tr("Ouvrir la Vue"), vueList_);
    QAction* principaleAction = new QAction(tr("★ Choisir comme Vue principale"), vueList_);
    QAction* fileAction = new QAction(tr("Classer la Vue…"), vueList_);
    QAction* removeAction = new QAction(tr("Supprimer la Vue…"), vueList_);
    removeAction->setShortcut(QKeySequence::Delete);
    removeAction->setShortcutContext(Qt::WidgetShortcut);
    vueList_->addActions({openAction, principaleAction, fileAction, removeAction});
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->addWidget(instructions_);
    sideLayout->addWidget(unsortedButton_);
    sideLayout->addWidget(vuesTitle_);
    sideLayout->addWidget(vueList_, 1);
    auto* vueButtons = new QHBoxLayout;
    vueButtons->addWidget(open);
    vueButtons->addWidget(principale);
    sideLayout->addLayout(vueButtons);
    auto* moreButtons = new QHBoxLayout;
    moreButtons->addWidget(file);
    moreButtons->addWidget(remove);
    sideLayout->addLayout(moreButtons);
    auto* dock = new QDockWidget(tr("TP et Vues"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(side);
    dock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Hides the panel for a bigger live view; the toolbars keep showing the
    // Échantillon and Grossissement a capture goes to, and capturing works
    // the same. Remembered on this PC.
    how->addSeparator();
    QAction* panel = how->addAction(QString());
    panel->setShortcut(QKeySequence(Qt::Key_F9));
    panel->setToolTip(tr("Masque ou affiche le panneau TP et Vues, pour agrandir l'image en direct."));
    const auto showPanel = [dock, panel](bool visible) {
        dock->setVisible(visible);
        panel->setText(visible ? MainWindow::tr("Masquer les Vues (F9)") : MainWindow::tr("Afficher les Vues (F9)"));
    };
    showPanel(uiSetting(setup_.configPath, kPanelVisibleKey, true).toBool());
    connect(panel, &QAction::triggered, this, [this, dock, showPanel] {
        const bool visible = dock->isHidden();
        showPanel(visible);
        setUiSetting(setup_.configPath, kPanelVisibleKey, visible);
    });

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
    connect(file, &QPushButton::clicked, this, &MainWindow::fileSelectedVue);
    connect(remove, &QPushButton::clicked, this, &MainWindow::deleteSelectedVue);
    connect(openAction, &QAction::triggered, this, &MainWindow::openVue);
    connect(principaleAction, &QAction::triggered, this, &MainWindow::flagPrincipale);
    connect(fileAction, &QAction::triggered, this, &MainWindow::fileSelectedVue);
    connect(removeAction, &QAction::triggered, this, &MainWindow::deleteSelectedVue);
    connect(vueList_, &QListWidget::itemActivated, this, &MainWindow::openVue);
    connect(unsortedButton_, &QPushButton::toggled, this, &MainWindow::refreshVues);
    connect(grossissement_, &QComboBox::currentTextChanged, this, &MainWindow::refreshCaptureState);
    connect(picker_, &LocationPicker::locationChanged, this, [this] {
        // Picking an Échantillon shows its Vues.
        const QSignalBlocker blocker(unsortedButton_);
        unsortedButton_->setChecked(false);
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

    QMenu* fiche = menuBar()->addMenu(tr("&Fiche"));
    connect(fiche->addAction(tr("Générer la Fiche du TP…")), &QAction::triggered, this, &MainWindow::generateFiche);
}

void MainWindow::generateFiche() {
    const auto where = location();
    const Tp* tp = picker_->tp();
    if (!where || where->isUnsorted() || tp == nullptr || tp->echantillons.empty()) {
        QMessageBox::information(this, tr("Fiche du TP"),
                                 tr("Choisissez d'abord un TP du catalogue qui a des Échantillons."));
        return;
    }
    QStringList missing;
    for (const FicheEntry& entry : ficheEntries(store_, where->department, *tp)) {
        if (!entry.vue) missing << QString::fromStdString(entry.echantillon);
    }
    if (!missing.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Fiche du TP"),
            tr("Ces Échantillons n'ont pas de Vue principale :\n%1\n\nGénérer quand même la Fiche ? Un cadre vide les "
               "signalera.")
                .arg(missing.join(QLatin1Char('\n'))));
        if (answer != QMessageBox::Yes) return;
    }

    // Suggested beside the TP's Échantillon folders.
    const std::filesystem::path tpFolder = store_.folder(*where).parent_path();
    std::error_code ignored;
    std::filesystem::create_directories(tpFolder, ignored);
    const QString suggested =
        toQString(tpFolder / std::filesystem::u8path("Fiche " + vueFolderName(tp->name) + ".pdf"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer la Fiche du TP"), suggested, tr("PDF (*.pdf)"));
    if (path.isEmpty()) return;

    QStringList notes;
    try {
        const WaitCursor wait;
        notes = writeFichePdf(store_, where->department, *tp, path);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Fiche du TP"), QString::fromStdString(e.what()));
        return;
    }
    QString message = tr("Fiche enregistrée :\n%1").arg(path);
    if (!notes.isEmpty()) message += tr("\n\nÀ vérifier :\n%1").arg(notes.join(QLatin1Char('\n')));
    message += tr("\n\nL'ouvrir pour l'imprimer ?");
    if (QMessageBox::question(this, tr("Fiche du TP"), message) == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
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

std::optional<VueLocation> MainWindow::listedLocation() const {
    if (unsortedButton_->isChecked()) return VueLocation::unsorted();
    return location();
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
    // Its own mire Calibration, or one derived from the objective measured
    // last (ADR 0020).
    const auto stored = setup_.config.effectiveCalibration(name.toStdString());
    if (!stored) {
        calibrationLabel_->setText(tr(" non calibré "));
        captureWarning_ = tr("%1 n'est pas calibré sur ce poste : les Vues seront prises sans Échelle, et sans "
                             "mesure possible. Menu Poste › Calibrer un grossissement : un seul objectif calibré "
                             "sur la mire suffit, les autres en sont déduits.")
                              .arg(name);
    } else {
        const QString origin = stored->isDerived()
                                   ? tr(" (déduit de %1)").arg(QString::fromStdString(stored->derivedFrom))
                                   : QString();
        calibrationLabel_->setText(tr(" %1 px/µm%2, image %3x%4 ")
                                       .arg(stored->pixelsPerMicron, 0, 'f', 4)
                                       .arg(origin)
                                       .arg(stored->frameWidth)
                                       .arg(stored->frameHeight));
        if (frameSize_.isValid() && !stored->fitsFrame(frameSize_.width(), frameSize_.height())) {
            // ADR 0015: never applied to another frame size, never rescaled.
            const QString measured =
                QString::fromStdString(stored->isDerived() ? stored->derivedFrom : stored->grossissement);
            captureWarning_ = tr("%1 a été calibré en %2x%3 mais la caméra fournit %4x%5 : les Vues seront prises "
                                 "sans Calibration. Reconnectez la caméra pour retrouver ce mode, ou recalibrez %1.")
                                  .arg(measured)
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

    // Without a catalog the picker itself is on Unsorted (ADR 0008); with
    // one, the Vues still there are listed on request, until all are filed.
    int unsorted = 0;
    if (!setup_.catalog.empty()) {
        try {
            unsorted = static_cast<int>(store_.listVues(VueLocation::unsorted()).size());
        } catch (const std::exception&) {
            unsorted = 0;  // an unreadable bucket: nothing to offer
        }
    }
    if (unsorted == 0 && unsortedButton_->isChecked()) {
        const QSignalBlocker blocker(unsortedButton_);
        unsortedButton_->setChecked(false);
    }
    unsortedButton_->setText(tr("Vues non classées (%n)", nullptr, unsorted));
    unsortedButton_->setVisible(unsorted > 0);

    const auto where = listedLocation();
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
    const int count = static_cast<int>(vues.size());
    if (!where->isUnsorted()) {
        vuesTitle_->setText(tr("Vues de l'Échantillon : %n", nullptr, count));
    } else if (setup_.catalog.empty()) {
        vuesTitle_->setText(tr("Vues non classées : %n", nullptr, count));
    } else {
        vuesTitle_->setText(
            tr("Vues non classées : %n. Choisissez-en une, puis « Classer la Vue… » pour la ranger dans son Échantillon.",
               nullptr, count));
    }
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
    // Shows where the new Vue went.
    const QSignalBlocker blocker(unsortedButton_);
    unsortedButton_->setChecked(false);
    refreshVues();
}

void MainWindow::flagPrincipale() {
    const QListWidgetItem* item = vueList_->currentItem();
    const auto where = listedLocation();
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
    const auto where = listedLocation();
    if (item == nullptr || !where) return;
    try {
        Vue vue = Vue::load(toPath(item->data(kPathRole).toString()));
        auto* window = new VueWindow(std::move(vue), store_, *where, this);  // a window of its own
        connect(window, &VueWindow::principaleChanged, this, &MainWindow::refreshVues);
        connect(window, &VueWindow::deleteRequested, this, [this, window] {
            const std::filesystem::path vue = window->vuePath();  // the window may close
            deleteVue(vue);
        });
        window->show();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Vue illisible"), QString::fromStdString(e.what()));
    }
}

void MainWindow::fileSelectedVue() {
    const QListWidgetItem* item = vueList_->currentItem();
    if (item == nullptr) {
        QMessageBox::information(this, tr("Classer la Vue"), tr("Sélectionnez d'abord une Vue dans la liste."));
        return;
    }
    if (!hasEchantillon(setup_.catalog)) {
        QMessageBox::information(this, tr("Classer la Vue"),
                                 tr("Le catalogue de TP n'a encore aucun Échantillon où ranger la Vue : complétez-le "
                                    "(Poste › Modifier le catalogue de TP), puis rechargez-le."));
        return;
    }
    const std::filesystem::path vue = toPath(item->data(kPathRole).toString());
    // An open Vue would save its next Treatment beside the old path.
    for (VueWindow* window : findChildren<VueWindow*>()) {
        if (window->isVisible() && window->vuePath() == vue) {
            QMessageBox::information(this, tr("Classer la Vue"),
                                     tr("Cette Vue est ouverte : fermez sa fenêtre avant de la classer."));
            window->raise();
            window->activateWindow();
            return;
        }
    }

    const auto to = pickEchantillon(this, setup_.catalog, location(), vue, item->icon());
    if (!to) return;
    try {
        store_.fileVue(vue, *to);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Classer la Vue"), QString::fromStdString(e.what()));
        refreshVues();
        return;
    }
    statusBar()->showMessage(tr("Vue rangée dans %1 › %2 › %3")
                                 .arg(QString::fromStdString(to->department), QString::fromStdString(to->tp),
                                      QString::fromStdString(to->echantillon)),
                             10000);
    refreshVues();
}

void MainWindow::deleteSelectedVue() {
    const QListWidgetItem* item = vueList_->currentItem();
    if (item == nullptr) {
        QMessageBox::information(this, tr("Supprimer la Vue"), tr("Sélectionnez d'abord une Vue dans la liste."));
        return;
    }
    deleteVue(toPath(item->data(kPathRole).toString()));
}

void MainWindow::deleteVue(const std::filesystem::path& vue) {
    bool principale = false;
    if (const auto where = listedLocation()) {
        try {
            principale = store_.principale(*where) == vue;
        } catch (const std::exception&) {
            principale = false;  // an unreadable flag: nothing to warn about
        }
    }
    QString question = tr("Supprimer la Vue du %1 ?").arg(vueLabel(vue, false));
    if (principale) {
        question += tr("\n\nC'est la Vue principale de l'Échantillon : la Fiche du TP ne l'aura plus.");
    }
    question += tr("\n\nElle est déplacée, avec ses traitements, dans la Corbeille du dossier des Vues "
                   "(Poste › Ouvrir le dossier des Vues). Rien n'est effacé : elle peut y être reprise à la main.");
    const auto answer = QMessageBox::question(this, tr("Supprimer la Vue"), question,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // Each Treatment is already saved; an open window would save the next
    // one where the Vue no longer is.
    for (VueWindow* window : findChildren<VueWindow*>()) {
        if (window->vuePath() == vue) window->close();
    }
    try {
        store_.deleteVue(vue);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Supprimer la Vue"), QString::fromStdString(e.what()));
        refreshVues();
        return;
    }
    statusBar()->showMessage(tr("Vue déplacée dans la Corbeille du dossier des Vues"), 10000);
    refreshVues();
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
    auto* dialog =
        new MireDialog(grossissementChoices(setup_.config), grossissement(), setup_.config.mirePitchMicrons, this);
    const QPointer<MireDialog> guard(dialog);
    connect(dialog, &MireDialog::stillRequested, this, [this, guard] { requestStill(guard); });
    requestStill(guard);
    std::optional<Calibration> result;
    double mirePitch = setup_.config.mirePitchMicrons;
    if (dialog->exec() == QDialog::Accepted) {
        result = dialog->result();
        mirePitch = dialog->pitchMicrons();
    }
    delete dialog;
    if (!result) return;
    // The workstation's mire, for next time (#65); saved with the Calibration.
    setup_.config.mirePitchMicrons = mirePitch;

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
        statusBar()->showMessage(tr("Calibration de %1 enregistrée ; les grossissements non calibrés sur la mire en "
                                    "sont déduits.")
                                     .arg(QString::fromStdString(result->grossissement)),
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
        std::optional<cv::Vec2d> lineNormal;
        QString error;
        try {
            const cv::Mat frame = station.grabPreview();
            still = toQImage(frame);  // full size: the clicks are in its pixels
            lineNormal = mireLineNormal(frame);  // here, off the UI thread: a 4K frame takes a moment
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [dialog, still, lineNormal, error] {
                if (!dialog) return;
                if (error.isEmpty()) {
                    dialog->setStill(still, lineNormal);
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
    if (const auto where = listedLocation()) {
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
