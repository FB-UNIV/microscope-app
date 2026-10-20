#include "main_window.hpp"

#include "camera_setup.hpp"
#include "camera_setup_dialog.hpp"
#include "capture_station.hpp"
#include "fiche.hpp"
#include "fiche_pdf.hpp"
#include "location_picker.hpp"
#include "mire_dialog.hpp"
#include "preview_widget.hpp"
#include "preview_worker.hpp"
#include "image_io.hpp"
#include "qt_image.hpp"
#include "slot_guard.hpp"
#include "theme.hpp"
#include "vue_time.hpp"
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
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyledItemDelegate>
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
constexpr int kPrincipaleRole = Qt::UserRole + 2;
constexpr int kTakenRole = Qt::UserRole + 3;  // local QDateTime; invalid for a file VueStore didn't name
const QSize kThumbnailSize(192, 108);
const QSize kRowThumbnail(128, 72);  // as the list draws it

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

// "15/10/2026 17:43:53", or the bare name for a file VueStore didn't name.
QString vueLabel(const std::filesystem::path& vue, bool principale) {
    const QDateTime taken = vueTaken(vue);
    const QString label =
        taken.isValid() ? taken.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")) : toQString(vue.filename());
    return principale ? QStringLiteral("★ ") + label + MainWindow::tr(" — Vue principale") : label;
}

// A Vue of the list: its thumbnail, the time and date it was taken, and a
// badge on the Vue principale. Every row is as tall before its thumbnail
// is decoded as after, so the list doesn't jump while they load.
class VueItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& /*option*/, const QModelIndex& /*index*/) const override {
        return {kRowThumbnail.width() + 120, kRowThumbnail.height() + 16};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5);
        if (option.state & QStyle::State_Selected) {
            QColor fill = theme::accent;
            fill.setAlpha(0x33);
            QColor edge = theme::accent;
            edge.setAlpha(0x80);
            painter->setPen(edge);
            painter->setBrush(fill);
            painter->drawRoundedRect(row, 6, 6);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, 10));
            painter->drawRoundedRect(row, 6, 6);
        }

        // The thumbnail, on the canvas while it loads, with a neutral edge.
        // It fills its slot, cropped: every frame shape lines up the same.
        const QRect thumb(option.rect.topLeft() + QPoint(8, 8), kRowThumbnail);
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(thumb), 4, 4);
        painter->save();
        painter->setClipPath(rounded);
        painter->fillRect(thumb, theme::canvas);
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            const QPixmap pixmap = icon.pixmap(kThumbnailSize);
            QRect target(QPoint(0, 0), pixmap.size().scaled(thumb.size(), Qt::KeepAspectRatioByExpanding));
            target.moveCenter(thumb.center());
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawPixmap(target, pixmap);
        }
        painter->restore();
        painter->setPen(QColor(255, 255, 255, 26));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(thumb).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        // Time over date: within a session the time is what tells Vues apart.
        const QDateTime taken = index.data(kTakenRole).toDateTime();
        const int left = thumb.right() + 13;
        const int width = std::max(0, option.rect.right() - 8 - left);
        QFont strong = option.font;
        strong.setWeight(QFont::DemiBold);
        const QFontMetrics strongMetrics(strong);
        const QFontMetrics metrics(option.font);
        int y = thumb.top() + 2;
        painter->setFont(strong);
        painter->setPen(theme::text);
        const QString first = taken.isValid() ? taken.toString(QStringLiteral("HH:mm:ss"))
                                              : index.data(Qt::ToolTipRole).toString();
        painter->drawText(QRect(left, y, width, strongMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          strongMetrics.elidedText(first, Qt::ElideMiddle, width));
        y += strongMetrics.height() + 2;
        if (taken.isValid()) {
            painter->setFont(option.font);
            painter->setPen(theme::textMuted);
            painter->drawText(QRect(left, y, width, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                              taken.toString(QStringLiteral("dd/MM/yyyy")));
            y += metrics.height();
        }
        if (index.data(kPrincipaleRole).toBool()) {
            const QString badge = MainWindow::tr("★ Principale");
            const QRectF pill(left, thumb.bottom() - metrics.height() - 3, metrics.horizontalAdvance(badge) + 16,
                              metrics.height() + 4);
            QColor fill = theme::accent;
            fill.setAlpha(0x33);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
            painter->setFont(option.font);
            painter->setPen(theme::accentText);
            painter->drawText(pill, Qt::AlignCenter, badge);
        }
        painter->restore();
    }
};

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
    thumbnailPool_.setMaxThreadCount(1);  // one thumbnail at a time, in list order
    cameraSearchPool_.setMaxThreadCount(1);
    // The build in the title bar, so a screenshot from a lab PC says which
    // build it came from.
    const QString version = QCoreApplication::applicationVersion();
    setWindowTitle(version.isEmpty()
                       ? QGuiApplication::applicationDisplayName()
                       : QStringLiteral("%1 — %2").arg(QGuiApplication::applicationDisplayName(), version));
    resize(1400, 860);

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    theme::setRole(banner_, "banner", QStringLiteral("warning"));
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

    // How it is taken: the capture settings on the left, the camera and the
    // panel on the right.
    grossissement_ = new QComboBox;
    calibrationLabel_ = new QLabel;
    captureButton_ = new QPushButton(tr("Capturer (Espace)"));
    // Object names: how app/tests/test_app_e2e.cpp finds the widgets it drives.
    captureButton_->setObjectName(QStringLiteral("captureButton"));
    captureButton_->setShortcut(QKeySequence(Qt::Key_Space));
    theme::setRole(captureButton_, "variant", QStringLiteral("primary"));
    theme::setRole(captureButton_, "size", QStringLiteral("large"));
    QToolBar* how = addToolBar(tr("Capture"));
    how->setMovable(false);
    how->addWidget(new QLabel(tr("Grossissement")));
    how->addWidget(grossissement_);
    how->addWidget(calibrationLabel_);
    how->addSeparator();
    how->addWidget(captureButton_);
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    how->addWidget(spacer);
    QAction* reconnect = how->addAction(tr("Reconnecter la caméra"));
    reconnect->setShortcut(QKeySequence::Refresh);
    reconnect->setToolTip(tr("Reconnecter la caméra (%1)").arg(reconnect->shortcut().toString(QKeySequence::NativeText)));

    // The TP and the Échantillon's Vues.
    instructions_ = new QLabel;
    instructions_->setWordWrap(true);
    instructions_->setTextFormat(Qt::RichText);
    theme::setRole(instructions_, "callout", QStringLiteral("note"));
    unsortedButton_ = new QPushButton;
    unsortedButton_->setCheckable(true);
    unsortedButton_->setToolTip(tr("Vues prises sans catalogue de TP, à ranger dans leur Échantillon."));
    vuesTitle_ = new QLabel;
    vuesTitle_->setWordWrap(true);
    theme::setRole(vuesTitle_, "role", QStringLiteral("section"));
    vueList_ = new QListWidget;
    vueList_->setObjectName(QStringLiteral("vueList"));
    vueList_->setItemDelegate(new VueItemDelegate(vueList_));
    vueList_->setUniformItemSizes(true);
    vueList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    vueList_->setMouseTracking(true);
    vueList_->viewport()->setAttribute(Qt::WA_Hover);
    vueList_->setToolTip(tr("Double-clic : ouvrir la Vue."));
    noVues_ = new QLabel(tr("Aucune Vue.\nEspace pour capturer."));
    noVues_->setAlignment(Qt::AlignCenter);
    theme::setRole(noVues_, "role", QStringLiteral("muted"));
    noVues_->hide();
    openButton_ = new QPushButton(tr("Ouvrir la Vue"));
    openButton_->setObjectName(QStringLiteral("openVueButton"));
    theme::setRole(openButton_, "variant", QStringLiteral("primary"));
    principaleButton_ = new QPushButton(tr("★ Principale"));
    principaleButton_->setToolTip(tr("Choisir comme Vue principale : celle imprimée sur la Fiche du TP."));
    fileButton_ = new QPushButton(tr("Classer…"));
    fileButton_->setToolTip(tr("Ranger la Vue dans un autre Échantillon."));
    removeButton_ = new QPushButton(tr("Supprimer…"));
    removeButton_->setToolTip(tr("Déplacer la Vue dans la Corbeille (Suppr). Rien n'est effacé."));
    theme::setRole(removeButton_, "variant", QStringLiteral("danger"));
    // The same on the list itself: right-click, and Suppr.
    vueList_->setContextMenuPolicy(Qt::ActionsContextMenu);
    openAction_ = new QAction(tr("Ouvrir la Vue"), vueList_);
    principaleAction_ = new QAction(tr("★ Choisir comme Vue principale"), vueList_);
    fileAction_ = new QAction(tr("Classer la Vue…"), vueList_);
    removeAction_ = new QAction(tr("Supprimer la Vue…"), vueList_);
    removeAction_->setShortcut(QKeySequence::Delete);
    removeAction_->setShortcutContext(Qt::WidgetShortcut);
    vueList_->addActions({openAction_, principaleAction_, fileAction_, removeAction_});
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(12, 12, 12, 12);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(instructions_);
    sideLayout->addWidget(unsortedButton_);
    sideLayout->addSpacing(4);
    sideLayout->addWidget(vuesTitle_);
    sideLayout->addWidget(vueList_, 1);
    sideLayout->addWidget(noVues_, 1);
    auto* vueButtons = new QHBoxLayout;
    vueButtons->setSpacing(8);
    vueButtons->addWidget(openButton_, 1);
    vueButtons->addWidget(principaleButton_, 1);
    sideLayout->addLayout(vueButtons);
    auto* moreButtons = new QHBoxLayout;
    moreButtons->setSpacing(8);
    moreButtons->addWidget(fileButton_, 1);
    moreButtons->addWidget(removeButton_, 1);
    sideLayout->addLayout(moreButtons);
    auto* dock = new QDockWidget(tr("TP et Vues"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(side);
    dock->setMinimumWidth(320);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Hides the panel for a bigger live view; the toolbars keep showing the
    // Échantillon and Grossissement a capture goes to, and capturing works
    // the same. Remembered on this PC.
    QAction* panel = how->addAction(QString());
    panel->setShortcut(QKeySequence(Qt::Key_F9));
    panel->setToolTip(tr("Agrandir l'image en direct."));
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
    fpsLabel_->setObjectName(QStringLiteral("fpsLabel"));
    // As wide as the widest rate, so the status bar doesn't shift as it changes.
    fpsLabel_->setMinimumWidth(fpsLabel_->fontMetrics().horizontalAdvance(tr("%1 images/s").arg(88.8, 0, 'f', 1)) + 16);
    fpsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addWidget(modeLabel_);
    statusBar()->addPermanentWidget(fpsLabel_);
    // The OpenCV the app runs on, as camera_probe reports it. It also makes
    // the exe load the OpenCV DLL at start, so the CI launch check of the
    // Windows bundle proves the bundle ships it.
    auto* opencv = new QLabel(QStringLiteral("OpenCV %1").arg(QString::fromStdString(cv::getVersionString())));
    theme::setRole(opencv, "role", QStringLiteral("faint"));
    statusBar()->addPermanentWidget(opencv);

    buildMenus();
    fillGrossissements();
    picker_->setCatalog(setup_.catalog);

    connect(reconnect, &QAction::triggered, this, &MainWindow::startCamera);
    connect(captureButton_, &QPushButton::clicked, this, &MainWindow::capture);
    connect(principaleButton_, &QPushButton::clicked, this, &MainWindow::flagPrincipale);
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::openVue);
    connect(fileButton_, &QPushButton::clicked, this, &MainWindow::fileSelectedVue);
    connect(removeButton_, &QPushButton::clicked, this, &MainWindow::deleteSelectedVue);
    connect(openAction_, &QAction::triggered, this, &MainWindow::openVue);
    connect(principaleAction_, &QAction::triggered, this, &MainWindow::flagPrincipale);
    connect(fileAction_, &QAction::triggered, this, &MainWindow::fileSelectedVue);
    connect(removeAction_, &QAction::triggered, this, &MainWindow::deleteSelectedVue);
    connect(vueList_, &QListWidget::itemActivated, this, &MainWindow::openVue);
    connect(vueList_, &QListWidget::currentRowChanged, this, &MainWindow::refreshVueActions);
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
    // A camera search still running posts back to this window.
    cameraSearchPool_.waitForDone();
    // A thumbnail task still running posts back to this window.
    thumbnailPool_.clear();
    thumbnailPool_.waitForDone();
}

void MainWindow::buildMenus() {
    QMenu* poste = menuBar()->addMenu(tr("&Poste"));
    connect(poste->addAction(tr("Configurer la caméra…")), &QAction::triggered, this, &MainWindow::configureCamera);
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

    QMenu* help = menuBar()->addMenu(tr("&Aide"));
    connect(help->addAction(tr("Raccourcis clavier")), &QAction::triggered, this, &MainWindow::showShortcuts);
    connect(help->addAction(tr("À propos de Métallo")), &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showShortcuts() {
    const auto row = [](const QString& keys, const QString& action) {
        return QStringLiteral("<tr><td style=\"padding:3px 16px 3px 0\"><b>%1</b></td><td>%2</td></tr>")
            .arg(keys.toHtmlEscaped(), action.toHtmlEscaped());
    };
    QString html = QStringLiteral("<table>");
    html += row(tr("Espace"), tr("Capturer"));
    html += row(tr("F5"), tr("Reconnecter la caméra"));
    html += row(tr("F9"), tr("Masquer ou afficher les Vues"));
    html += row(tr("Entrée, double-clic"), tr("Ouvrir la Vue"));
    html += row(tr("Suppr"), tr("Supprimer la Vue"));
    html += QStringLiteral("<tr><td colspan=\"2\" style=\"padding-top:10px\"><b>%1</b></td></tr>")
                .arg(tr("Dans une Vue").toHtmlEscaped());
    html += row(tr("Molette, + et −"), tr("Zoom"));
    html += row(tr("0 / 1"), tr("Toute l'image / 100 %"));
    html += row(tr("Glisser"), tr("Déplacer l'image"));
    html += row(tr("Ctrl+Z / Ctrl+Y"), tr("Annuler / rétablir un traitement"));
    html += row(tr("Retour arrière"), tr("Annuler le dernier point"));
    html += row(tr("Échap"), tr("Ranger l'outil"));
    html += QStringLiteral("</table>");
    QMessageBox box(QMessageBox::NoIcon, tr("Raccourcis clavier"), html, QMessageBox::Close, this);
    box.setTextFormat(Qt::RichText);
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle(tr("À propos de Métallo"));
    box.setIconPixmap(theme::appIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<h3 style=\"margin:0\">Métallo</h3><p>%1</p><p>%2<br>Qt %3 · OpenCV %4</p>")
                    .arg(tr("Capture, mesures et analyse de phases au microscope métallographique."),
                         tr("Version %1").arg(QCoreApplication::applicationVersion().toHtmlEscaped()),
                         QString::fromLatin1(qVersion()), QString::fromStdString(cv::getVersionString())));
    box.exec();
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
            tr("Sans Vue principale :\n%1\n\nGénérer la Fiche quand même ? Un cadre vide les remplacera.")
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
    // findCameras() has the camera: it restarts once the search is done.
    if (searching_) return;
    const int generation = ++generation_;
    const CameraConfig camera = trialCamera_ ? *trialCamera_ : setup_.camera;
    preview_->showBusy(tr("Ouverture de la caméra…"));
    if (setupDialog_) setupDialog_->cameraStarting();
    modeLabel_->setText(requestedMode(camera));
    fpsLabel_->clear();
    cameraWarnings_.clear();

    jobs_ = std::make_shared<CameraJobs>();
    thread_ = new QThread(this);
    auto* worker = new PreviewWorker(camera, jobs_);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &PreviewWorker::run);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);

    // Queued to this thread. Each lambda checks the generation first: a
    // worker that was stopped may still have signals in the queue.
    connect(worker, &PreviewWorker::opened, this,
            [this, generation](const QString& mode, const QStringList& warnings, const ProfileReport& report) {
                if (generation != generation_) return;
                cameraOpen_ = true;
                report_ = report;
                modeLabel_->setText(tr("Caméra : %1").arg(mode));
                cameraWarnings_ = warnings;
                refreshCaptureState();
                if (setupDialog_) setupDialog_->cameraOpened(report);
                if (calibrateWhenOpen_ && !setupDialog_) {
                    calibrateWhenOpen_ = false;
                    QTimer::singleShot(0, this, &MainWindow::calibrateGrossissement);
                }
            });
    connect(worker, &PreviewWorker::failed, this, [this, generation](const QString& message) {
        if (generation != generation_) return;
        cameraOpen_ = false;
        busy_ = false;
        calibrateWhenOpen_ = false;
        preview_->showMessage(tr("Caméra indisponible\n\n%1\n\nFermez MotiConnect, puis Reconnecter la caméra (F5).")
                                  .arg(message));
        if (setupDialog_) setupDialog_->cameraFailed(message);
        fpsLabel_->clear();
        refreshCaptureState();
    });
    // The pending flag, not the worker: it may be deleted before this runs.
    connect(worker, &PreviewWorker::frameReady, this,
            [this, generation, pending = worker->framePending()](const QImage& frame, const QSize& fullSize) {
                pending->store(false);
                if (generation != generation_) return;
                // Only one view paints: the wizard's while it is open.
                if (setupDialog_) {
                    setupDialog_->setFrame(frame);
                } else {
                    preview_->setFrame(frame);
                }
                if (fullSize != frameSize_) {
                    frameSize_ = fullSize;
                    refreshCaptureState();
                }
            });
    connect(worker, &PreviewWorker::fpsMeasured, this, [this, generation](double fps) {
        if (generation != generation_) return;
        fpsLabel_->setText(tr("%1 images/s").arg(fps, 0, 'f', 1));
        if (setupDialog_) setupDialog_->setFps(fps);
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
    report_ = ProfileReport();
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
        calibrationLabel_->setText(tr("Non calibré"));
        calibrationLabel_->setToolTip(QString());
        theme::setRole(calibrationLabel_, "chip", QStringLiteral("warning"));
        captureWarning_ = tr("%1 non calibré : Vues sans Échelle ni mesure. Poste › Calibrer un grossissement (un "
                             "seul objectif suffit).")
                              .arg(name);
    } else {
        calibrationLabel_->setText(tr("%1 px/µm · %2×%3")
                                       .arg(stored->pixelsPerMicron, 0, 'f', 4)
                                       .arg(stored->frameWidth)
                                       .arg(stored->frameHeight));
        calibrationLabel_->setToolTip(
            stored->isDerived() ? tr("Déduit de %1").arg(QString::fromStdString(stored->derivedFrom))
                                : tr("Mesuré sur la mire"));
        theme::setRole(calibrationLabel_, "chip", QStringLiteral("ok"));
        if (frameSize_.isValid() && !stored->fitsFrame(frameSize_.width(), frameSize_.height())) {
            // ADR 0015: never applied to another frame size, never rescaled.
            const QString measured =
                QString::fromStdString(stored->isDerived() ? stored->derivedFrom : stored->grossissement);
            theme::setRole(calibrationLabel_, "chip", QStringLiteral("warning"));
            captureWarning_ = tr("%1 calibré en %2×%3, la caméra fournit %4×%5 : Vues sans Calibration. Reconnectez "
                                 "la caméra ou recalibrez %1.")
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
    const QString instructions = tp ? QString::fromStdString(tp->instructions).trimmed() : QString();
    if (!instructions.isEmpty()) {
        instructions_->setText(QStringLiteral("<b>%1</b><br>%2").arg(QString::fromStdString(tp->name).toHtmlEscaped(),
                                                                      instructions.toHtmlEscaped()));
    }
    instructions_->setVisible(!instructions.isEmpty());

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

    // Shows the list, or what stands in for it when it is empty.
    const auto showList = [this](bool list, const QString& empty) {
        vueList_->setVisible(list);
        noVues_->setText(empty);
        noVues_->setVisible(!list);
        refreshVueActions();
    };
    const auto where = listedLocation();
    if (!where) {
        vuesTitle_->setText(tr("Vues"));
        showList(false, tr("Ce TP n'a pas d'Échantillon :\ncomplétez le catalogue."));
        return;
    }
    std::vector<std::filesystem::path> vues;
    std::optional<std::filesystem::path> principale;
    try {
        vues = store_.listVues(*where);
        principale = store_.principale(*where);
    } catch (const std::exception& e) {
        vuesTitle_->setText(tr("Vues"));
        showList(false, QString::fromStdString(e.what()));
        return;
    }
    const int count = static_cast<int>(vues.size());
    if (!where->isUnsorted()) {
        vuesTitle_->setText(tr("Vues de l'Échantillon (%n)", nullptr, count));
    } else if (setup_.catalog.empty()) {
        vuesTitle_->setText(tr("Vues non classées (%n)", nullptr, count));
    } else {
        vuesTitle_->setText(tr("Vues non classées (%n) — à ranger avec « Classer… »", nullptr, count));
    }
    for (auto it = vues.rbegin(); it != vues.rend(); ++it) {  // newest first
        const bool flagged = principale && *principale == *it;
        auto* item = new QListWidgetItem(vueLabel(*it, flagged), vueList_);
        item->setData(kPathRole, toQString(*it));
        item->setData(kPrincipaleRole, flagged);
        item->setData(kTakenRole, vueTaken(*it));
        item->setToolTip(toQString(it->filename()));
    }
    showList(count > 0, tr("Aucune Vue.\nEspace pour capturer."));
    QTimer::singleShot(0, this, &MainWindow::loadNextThumbnail);
}

void MainWindow::refreshVueActions() {
    const QListWidgetItem* item = vueList_->currentItem();
    const bool picked = item != nullptr && !vueList_->isHidden();  // hidden: noVues_ stands in for it
    const bool flagged = picked && item->data(kPrincipaleRole).toBool();
    for (QPushButton* button : {openButton_, fileButton_, removeButton_}) button->setEnabled(picked);
    for (QAction* action : {openAction_, fileAction_, removeAction_}) action->setEnabled(picked);
    principaleButton_->setEnabled(picked && !flagged);
    principaleAction_->setEnabled(picked && !flagged);
}

void MainWindow::loadNextThumbnail() {
    // A thumbnail decoded before is set at once; the first one missing is
    // decoded off the GUI thread, and thumbnailLoaded() asks for the next.
    if (thumbnailLoading_) return;
    for (int i = 0; i < vueList_->count(); ++i) {
        QListWidgetItem* item = vueList_->item(i);
        if (item->data(kThumbnailRole).toBool()) continue;
        const QString path = item->data(kPathRole).toString();
        if (const auto known = thumbnails_.constFind(path); known != thumbnails_.constEnd()) {
            item->setIcon(*known);
            item->setData(kThumbnailRole, true);
            continue;
        }
        thumbnailLoading_ = true;
        // ~MainWindow waits for the pool, so `this` outlives the task.
        thumbnailPool_.start([this, path] {
            QImage thumbnail;
            try {
                // Through the wide path: "Fonte γ" has no ANSI name (review H1).
                const cv::Mat image = readImage(toPath(path), cv::IMREAD_REDUCED_COLOR_4);
                if (!image.empty()) thumbnail = toQImage(image, kThumbnailSize);
            } catch (const std::exception&) {
                // Unreadable: the Vue stays listed, without a thumbnail.
            }
            QMetaObject::invokeMethod(
                this, [this, path, thumbnail] { thumbnailLoaded(path, thumbnail); }, Qt::QueuedConnection);
        });
        return;
    }
}

void MainWindow::thumbnailLoaded(const QString& path, const QImage& thumbnail) {
    thumbnailLoading_ = false;
    // Kept even when null, so an unreadable file isn't read again at every refresh.
    const QIcon icon = thumbnail.isNull() ? QIcon() : QIcon(QPixmap::fromImage(thumbnail));
    thumbnails_.insert(path, icon);
    // The list may have been rebuilt meanwhile: find the Vue's item again.
    for (int i = 0; i < vueList_->count(); ++i) {
        QListWidgetItem* item = vueList_->item(i);
        if (item->data(kPathRole).toString() == path) {
            item->setIcon(icon);
            item->setData(kThumbnailRole, true);
        }
    }
    loadNextThumbnail();
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
    preview_->flash();
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
    // Shows where the new Vue went, picked: newest first.
    const QSignalBlocker blocker(unsortedButton_);
    unsortedButton_->setChecked(false);
    refreshVues();
    if (vueList_->count() > 0) vueList_->setCurrentRow(0);
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
                                 tr("Le catalogue de TP n'a aucun Échantillon. Complétez-le (Poste › Modifier le "
                                    "catalogue de TP), puis rechargez-le."));
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
    if (principale) question += tr("\n\nC'est la Vue principale : la Fiche du TP ne l'aura plus.");
    question += tr("\n\nElle part dans la Corbeille du dossier des Vues, avec ses traitements. Rien n'est effacé.");
    const auto answer = QMessageBox::question(this, tr("Supprimer la Vue"), question,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // Each Treatment is already saved; an open window would save the next
    // one where the Vue no longer is.
    for (VueWindow* window : findChildren<VueWindow*>()) {
        if (window->vuePath() != vue) continue;
        window->discardPendingChanges();  // the Vue goes: nothing to ask about
        window->close();
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
            tr("Remplacer la Calibration de %1 (%2 px/µm, %3×%4) ? Les Vues déjà prises gardent la leur.")
                .arg(QString::fromStdString(existing->grossissement))
                .arg(existing->pixelsPerMicron, 0, 'f', 4)
                .arg(existing->frameWidth)
                .arg(existing->frameHeight));
        if (answer != QMessageBox::Yes) return;
    }
    setup_.config.setCalibration(*result);
    if (saveConfig()) {
        statusBar()->showMessage(tr("Calibration de %1 enregistrée. Les autres grossissements en sont déduits.")
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

void MainWindow::configureCamera() {
    if (setupDialog_) return;
    // From the camera running now — this run's --device or --api included —
    // unless it is replaying image files: then from the saved profile, or
    // the A5's for a workstation set up without a camera.
    CameraConfig start = setup_.camera.backend == "opencv" ? setup_.camera : setup_.config.camera;
    if (start.backend != "opencv") {
        start = moticamA5Profile();
        start.whiteReference = setup_.config.camera.whiteReference;
    }
    CameraSetupDialog dialog(setup_.config, start, platformCaptureApis(), this);
    connect(&dialog, &CameraSetupDialog::searchRequested, this, &MainWindow::searchCameras);
    connect(&dialog, &CameraSetupDialog::profileRequested, this, [this](const CameraConfig& profile) {
        trialCamera_ = profile;
        startCamera();
    });
    connect(&dialog, &CameraSetupDialog::controlsRequested, this, &MainWindow::tryCameraControls);
    connect(&dialog, &CameraSetupDialog::whiteReferenceRequested, this, &MainWindow::measureSetupWhiteReference);
    setupDialog_ = &dialog;
    trialCamera_ = start;
    if (start.toJson() != setup_.camera.toJson()) {
        startCamera();
    } else if (cameraOpen_) {
        dialog.cameraOpened(report_);
    }

    const bool accepted = dialog.exec() == QDialog::Accepted;
    const CameraSetupReview review = dialog.review();
    const bool recalibrate = dialog.recalibrateRequested();
    setupDialog_ = nullptr;
    trialCamera_.reset();
    if (accepted && review.changed) {
        // What the camera granted, never what it refused as if granted.
        setup_.config.camera = review.profile;
        setup_.camera = review.profile;
        if (saveConfig()) statusBar()->showMessage(tr("Profil caméra enregistré."), 10000);
    }
    // ADR 0015: the mode the Calibrations no longer fit; once the camera
    // runs it again, the mire dialog opens.
    calibrateWhenOpen_ = accepted && recalibrate;
    startCamera();  // the saved profile again, or the new one
}

void MainWindow::searchCameras() {
    if (searching_) return;
    stopCamera();
    searching_ = true;
    preview_->showBusy(tr("Recherche des caméras…"));
    if (setupDialog_) setupDialog_->showMessage(tr("Recherche des caméras…"));
    // ~MainWindow waits for the pool, so `this` outlives the task.
    cameraSearchPool_.start([this] {
        std::vector<FoundCamera> found;
        QString error;
        try {
            found = findCameras(platformCaptureApis());
        } catch (const std::exception& e) {
            error = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, found, error] {
                searching_ = false;
                if (setupDialog_) setupDialog_->camerasFound(found, error);
                startCamera();
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::tryCameraControls(const CameraConfig& profile) {
    trialCamera_ = profile;  // a restart keeps them
    if (!cameraOpen_ || !jobs_) {
        startCamera();
        return;
    }
    const int generation = generation_;
    const std::optional<double> exposureMs = profile.exposureMs;
    const std::optional<double> kelvin = profile.whiteBalanceKelvin;
    jobs_->post([this, generation, exposureMs, kelvin](CaptureStation& station) {
        const ProfileReport report = station.setControls(exposureMs, kelvin);
        QMetaObject::invokeMethod(
            this,
            [this, generation, report] {
                if (generation != generation_) return;  // that camera is gone: the new one reports at open
                report_ = report;
                if (setupDialog_) setupDialog_->controlsApplied(report);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::measureSetupWhiteReference() {
    if (!cameraOpen_ || !jobs_) {
        if (setupDialog_) setupDialog_->whiteReferenceFailed(tr("La caméra n'est pas disponible."));
        return;
    }
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
                if (!setupDialog_) return;
                if (generation != generation_ && error.isEmpty()) {
                    setupDialog_->whiteReferenceFailed(tr("La caméra a redémarré pendant la mesure : mesurez à nouveau."));
                    return;
                }
                if (!gains) {
                    setupDialog_->whiteReferenceFailed(error);
                    return;
                }
                if (trialCamera_) trialCamera_->whiteReference = *gains;  // a restart keeps them
                setupDialog_->whiteReferenceMeasured(*gains);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::openCatalogFile() {
    // exists() throws for a folder it may not look into (review H3).
    guardSlot(this, tr("Catalogue de TP"), [this] {
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
        QMessageBox::information(
            this, tr("Catalogue de TP"),
            tr("Une fois le fichier enregistré, choisissez Poste › Recharger le catalogue de TP."));
    });
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
