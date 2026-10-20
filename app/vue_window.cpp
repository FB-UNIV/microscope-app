#include "vue_window.hpp"

#include "echelle_treatment.hpp"
#include "histogram_widget.hpp"
#include "image_view.hpp"
#include "levels_treatment.hpp"
#include "levels_widget.hpp"
#include "measurement.hpp"
#include "phase_overlay_treatment.hpp"
#include "qt_image.hpp"
#include "slot_guard.hpp"
#include "theme.hpp"
#include "vickers.hpp"
#include "vue_time.hpp"
#include "wait_cursor.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRectF>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

namespace msapp {

namespace {

// How many pipeline edits undo goes back through.
constexpr std::size_t kHistory = 50;

QString describe(const Treatment& treatment) {
    if (const auto* levels = dynamic_cast<const LevelsTreatment*>(&treatment)) {
        return VueWindow::tr("Niveaux : noir %1, blanc %2").arg(levels->blackPoint()).arg(levels->whitePoint());
    }
    if (const auto* echelle = dynamic_cast<const EchelleTreatment*>(&treatment)) {
        return VueWindow::tr("Échelle (%1)").arg(QString::fromStdString(echelle->calibration().grossissement));
    }
    if (const auto* overlay = dynamic_cast<const PhaseOverlayTreatment*>(&treatment)) {
        return overlay->zone() ? VueWindow::tr("Carte des phases (seuil %1, sur une zone)").arg(overlay->threshold())
                               : VueWindow::tr("Carte des phases (seuil %1)").arg(overlay->threshold());
    }
    return QString::fromStdString(treatment.type());
}

// Measurement labels are plain ASCII ("2.5 um") for the Échelle's Hershey
// font; the window can show the real unit.
QString micronsLabel(const std::string& label) {
    return QString::fromStdString(label).replace(QStringLiteral(" um"), QStringLiteral(" µm"));
}

// Marks are kept in image pixels; the view speaks QPointF.
cv::Point2d toImagePoint(QPointF point) { return {point.x(), point.y()}; }
QPointF toViewPoint(cv::Point2d point) { return {point.x, point.y}; }

// French puts a space before a colon: a non-breaking one, so the colon
// never starts a line.
QString keepColons(QString text) {
    return text.replace(QStringLiteral(" :"), QString(QChar(0x00A0)) + QLatin1Char(':'));
}

void showText(QLabel* label, const QString& text, const char* role) {
    label->setText(text);
    theme::setRole(label, "role", QString::fromLatin1(role));
}

QLabel* sectionTitle(const QString& text) {
    auto* label = new QLabel(text);
    theme::setRole(label, "role", QStringLiteral("section"));
    return label;
}

// A tab's page, scrolled when the window is short, wrapped to its width.
QScrollArea* scrolled(QWidget* page, const char* name) {
    auto* area = new QScrollArea;
    area->setObjectName(QString::fromLatin1(name));
    area->setWidget(page);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return area;
}

// The capture details, a muted name beside each value.
QString captureText(const Vue& vue) {
    QList<std::pair<QString, QString>> rows;
    rows.append({VueWindow::tr("Fichier"), toQString(vue.imagePath().filename())});
    rows.append({VueWindow::tr("Image"), QStringLiteral("%1×%2").arg(vue.original().cols).arg(vue.original().rows)});
    if (const auto& calibration = vue.calibration()) {
        rows.append({VueWindow::tr("Calibration"),
                     VueWindow::tr("%1, %2 px/µm%3")
                         .arg(QString::fromStdString(calibration->grossissement))
                         .arg(calibration->pixelsPerMicron, 0, 'f', 4)
                         .arg(calibration->isDerived()
                                  ? VueWindow::tr(" (déduite de %1)").arg(QString::fromStdString(calibration->derivedFrom))
                                  : QString())});
    } else {
        rows.append({VueWindow::tr("Calibration"), VueWindow::tr("aucune : ni Échelle ni mesure")});
    }
    if (const auto& info = vue.captureInfo()) {
        const QString unknown = VueWindow::tr("auto ou inconnue");
        rows.append({VueWindow::tr("Exposition"),
                     info->exposureMs ? QStringLiteral("%1 ms").arg(*info->exposureMs) : unknown});
        rows.append({VueWindow::tr("Couleur"),
                     info->whiteBalanceKelvin ? QStringLiteral("%1 K").arg(*info->whiteBalanceKelvin) : unknown});
        rows.append({VueWindow::tr("Réf. blanche"), VueWindow::tr("R %1 · V %2 · B %3")
                                                        .arg(info->whiteReference.red, 0, 'f', 3)
                                                        .arg(info->whiteReference.green, 0, 'f', 3)
                                                        .arg(info->whiteReference.blue, 0, 'f', 3)});
    }
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"2\">");
    for (const auto& [name, value] : rows) {
        // The file name may break after its _ and -, so it wraps instead of
        // widening the panel.
        QString breakable = value.toHtmlEscaped();
        breakable.replace(QLatin1Char('_'), QStringLiteral("_&#8203;")).replace(QLatin1Char('-'), QStringLiteral("-&#8203;"));
        html += QStringLiteral("<tr><td style=\"color:%1; padding-right:12px; white-space:nowrap\">%2</td>"
                               "<td>%3</td></tr>")
                    .arg(theme::textMuted.name(), name.toHtmlEscaped(), breakable);
    }
    return html + QStringLiteral("</table>");
}

} // namespace

VueWindow::VueWindow(Vue vue, VueStore store, VueLocation location, QWidget* parent)
    : QMainWindow(parent), vue_(std::move(vue)), store_(std::move(store)), location_(std::move(location)) {
    setAttribute(Qt::WA_DeleteOnClose);
    // As the student knows the Vue: its Échantillon and when it was taken.
    const QDateTime taken = vueTaken(vue_.imagePath());
    setWindowTitle(QStringLiteral("%1 — %2")
                       .arg(location_.isUnsorted() ? tr("Vue non classée") : QString::fromStdString(location_.echantillon),
                            taken.isValid() ? taken.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))
                                            : toQString(vue_.imagePath().filename())));
    resize(1400, 900);
    try {
        levelsHistogram_ = levelsHistogram(vue_.original());
    } catch (const std::exception&) {
        // Not an 8-bit colour or grey photo: no histogram under the handles.
    }

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    theme::setRole(banner_, "banner", QStringLiteral("danger"));
    // ADR 0003/0015: what the sidecar lost, while the photo itself loaded.
    if (const auto& error = vue_.sidecarError()) {
        banner_->setText(tr("⚠ Traitements en partie illisibles : le fichier sera mis de côté à la prochaine "
                            "modification.\n%1")
                             .arg(QString::fromStdString(*error)));
    } else {
        banner_->hide();
    }
    view_ = new ImageView;
    // Object names: how app/tests/test_app_e2e.cpp finds the widgets it drives.
    view_->setObjectName(QStringLiteral("imageView"));
    auto* central = new QWidget;
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(banner_);
    centralLayout->addWidget(view_, 1);
    setCentralWidget(central);

    // The tools that act on the photo, above it (#89). The active tool's
    // button stays pressed; pressing it again, or Échap, puts it away.
    QToolBar* tools = addToolBar(tr("Outils"));
    tools->setMovable(false);
    undo_ = tools->addAction(tr("Annuler"));
    undo_->setObjectName(QStringLiteral("undoAction"));
    undo_->setShortcut(QKeySequence::Undo);
    undo_->setToolTip(tr("Annuler la dernière modification des traitements (%1)")
                          .arg(undo_->shortcut().toString(QKeySequence::NativeText)));
    redo_ = tools->addAction(tr("Rétablir"));
    redo_->setObjectName(QStringLiteral("redoAction"));
    redo_->setShortcut(QKeySequence::Redo);
    redo_->setToolTip(tr("Rétablir la modification annulée (%1)")
                          .arg(redo_->shortcut().toString(QKeySequence::NativeText)));
    tools->addSeparator();
    const auto toolButton = [tools](const QString& text, const char* name, const QString& tip) {
        auto* button = new QToolButton;
        button->setText(text);
        button->setObjectName(QString::fromLatin1(name));
        button->setCheckable(true);
        button->setToolTip(tip);
        tools->addWidget(button);
        return button;
    };
    lengthTool_ = toolButton(tr("Longueur"), "lengthTool", tr("Mesurer une longueur : deux clics."));
    vickersTool_ = toolButton(tr("Dureté Vickers"), "vickersTool",
                              tr("Dureté Vickers : les deux diagonales de l'empreinte, quatre clics."));
    load_ = new QComboBox;
    for (const double kgf : {0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 30.0}) {
        load_->addItem(QStringLiteral("HV%1 (%1 kgf)").arg(kgf), kgf);
    }
    load_->setCurrentIndex(load_->findData(kLegacyVickersLoadKgf));
    load_->setToolTip(tr("Charge de l'essai Vickers."));
    tools->addWidget(load_);
    tools->addSeparator();
    zoneTool_ = toolButton(tr("Zone"), "zoneTool",
                           tr("Glisser un rectangle : l'analyse de phases n'en compte que les pixels, pour exclure "
                              "poussières et inclusions. Plus la zone est petite, moins le pourcentage est sûr."));
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(spacer);
    QAction* zoomOut = tools->addAction(QStringLiteral("−"));
    zoomOut->setToolTip(tr("Zoom arrière (−)"));
    zoomLabel_ = new QLabel;
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setMinimumWidth(zoomLabel_->fontMetrics().horizontalAdvance(QStringLiteral("3200 %")) + 8);
    tools->addWidget(zoomLabel_);
    QAction* zoomIn = tools->addAction(QStringLiteral("+"));
    zoomIn->setToolTip(tr("Zoom avant (+)"));
    QAction* fit = tools->addAction(tr("Ajuster"));
    fit->setToolTip(tr("Toute l'image (0)"));
    QAction* actual = tools->addAction(QStringLiteral("100 %"));
    actual->setToolTip(tr("Un pixel de l'image par pixel de l'écran (1)"));

    positionLabel_ = new QLabel;
    positionLabel_->setMinimumWidth(
        positionLabel_->fontMetrics().horizontalAdvance(QStringLiteral("x 3840  y 2160 px  ·  2477.4 × 1393.5 µm")) + 16);
    positionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(positionLabel_);

    // Above the tabs: what the Vue is.
    auto* lens = new QLabel;
    if (const auto& calibration = vue_.calibration()) {
        lens->setText(tr("%1 · %2 px/µm")
                          .arg(QString::fromStdString(calibration->grossissement))
                          .arg(calibration->pixelsPerMicron, 0, 'f', 4));
        lens->setToolTip(calibration->isDerived()
                             ? tr("Déduite de %1").arg(QString::fromStdString(calibration->derivedFrom))
                             : tr("Mesurée sur la mire"));
        theme::setRole(lens, "chip", QStringLiteral("ok"));
    } else {
        lens->setText(tr("Sans Calibration"));
        lens->setToolTip(tr("Ni Échelle ni mesure sur cette Vue."));
        theme::setRole(lens, "chip", QStringLiteral("warning"));
    }
    principale_ = new QPushButton;
    auto* exportCopy = new QPushButton(tr("Exporter une copie…"));
    exportCopy->setToolTip(tr("La Vue avec ses traitements, en PNG, TIFF ou JPEG."));
    auto* lensRow = new QHBoxLayout;
    lensRow->addWidget(lens);
    lensRow->addStretch(1);
    auto* vueButtons = new QHBoxLayout;
    vueButtons->setSpacing(8);
    vueButtons->addWidget(principale_, 1);
    vueButtons->addWidget(exportCopy, 1);

    tabs_ = new QTabWidget;
    tabs_->setObjectName(QStringLiteral("vueTabs"));
    tabs_->setDocumentMode(true);
    imagePage_ = scrolled(buildImagePage(), "imagePage");
    measuresPage_ = scrolled(buildMeasuresPage(), "measuresPage");
    phasesPage_ = scrolled(buildPhasesPage(), "phasesPage");
    tabs_->addTab(imagePage_, tr("Image"));
    tabs_->addTab(measuresPage_, tr("Mesures"));
    tabs_->addTab(phasesPage_, tr("Phases"));

    // Under the tabs: the capture details, folded; deleting, away from the
    // everyday buttons — the main window asks first, then moves the Vue to
    // the Corbeille and closes this window.
    auto* info = new QLabel(captureText(vue_));
    info->setTextFormat(Qt::RichText);
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->hide();
    auto* details = new QToolButton;
    details->setText(tr("Détails de la prise de vue"));
    details->setCheckable(true);
    details->setArrowType(Qt::RightArrow);
    details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* deleteVue = new QPushButton(tr("Supprimer la Vue…"));
    deleteVue->setToolTip(tr("Déplacer la Vue dans la Corbeille. Rien n'est effacé."));
    theme::setRole(deleteVue, "variant", QStringLiteral("danger"));

    auto* panel = new QWidget;
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 12, 0, 12);
    panelLayout->setSpacing(8);
    auto* header = new QVBoxLayout;
    header->setContentsMargins(12, 0, 12, 0);
    header->setSpacing(8);
    header->addLayout(lensRow);
    header->addLayout(vueButtons);
    panelLayout->addLayout(header);
    panelLayout->addWidget(tabs_, 1);
    auto* footer = new QVBoxLayout;
    footer->setContentsMargins(12, 0, 12, 0);
    footer->setSpacing(8);
    footer->addWidget(details, 0, Qt::AlignLeft);
    footer->addWidget(info);
    footer->addWidget(deleteVue);
    panelLayout->addLayout(footer);
    auto* dock = new QDockWidget(tr("Vue"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(panel);
    dock->setMinimumWidth(360);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    if (!vue_.calibration()) {
        const QString why = tr("Vue sans Calibration : mesures impossibles.");
        lengthTool_->setEnabled(false);
        vickersTool_->setEnabled(false);
        lengthTool_->setToolTip(why);
        vickersTool_->setToolTip(why);
        showText(measureResult_, why, "muted");
    }

    connect(black_, &QSpinBox::valueChanged, this, [this](int value) {
        if (white_->value() <= value) white_->setValue(value + 1);
        levelsTimer_->start();
        refreshLevels();
    });
    connect(white_, &QSpinBox::valueChanged, this, [this](int value) {
        if (black_->value() >= value) black_->setValue(value - 1);
        levelsTimer_->start();
        refreshLevels();
    });
    connect(levels_, &LevelsWidget::levelsDragged, this, [this](int black, int white) {
        setLevelsFields(black, white);
        levelsTimer_->start();
        refreshLevels();
    });
    connect(levelsTimer_, &QTimer::timeout, this, &VueWindow::previewLevels);
    connect(applyLevels_, &QPushButton::clicked, this, &VueWindow::applyLevels);
    connect(ignoreLevels_, &QPushButton::clicked, this, [this] {
        showSavedLevels();
        guardSlot(this, tr("Affichage impossible"), [this] { showSaved(); });
    });
    connect(echelle_, &QPushButton::clicked, this, &VueWindow::addEchelle);
    connect(undo_, &QAction::triggered, this, [this] { stepHistory(true); });
    connect(redo_, &QAction::triggered, this, [this] { stepHistory(false); });
    connect(lengthTool_, &QToolButton::clicked, this, [this] { startTool(Tool::Length); });
    connect(vickersTool_, &QToolButton::clicked, this, [this] { startTool(Tool::Vickers); });
    connect(zoneTool_, &QToolButton::clicked, this, [this] { startTool(Tool::Zone); });
    connect(escape, &QShortcut::activated, this, &VueWindow::cancelTool);
    // A Vickers value follows the load picked, for every indent shown.
    connect(load_, &QComboBox::currentIndexChanged, this, [this] {
        redrawMarks();
        if (const auto current = marks_.current()) showResult(marks_.marks()[*current]);
    });
    connect(zoomOut, &QAction::triggered, view_, [this] { view_->zoomBy(0.8); });
    connect(zoomIn, &QAction::triggered, view_, [this] { view_->zoomBy(1.25); });
    connect(fit, &QAction::triggered, view_, &ImageView::zoomToFit);
    connect(actual, &QAction::triggered, view_, &ImageView::zoomToActualSize);
    connect(view_, &ImageView::zoomChanged, this,
            [this](double zoom) { zoomLabel_->setText(QStringLiteral("%1 %").arg(qRound(zoom * 100.0))); });
    connect(view_, &ImageView::hovered, this, &VueWindow::hovered);
    connect(view_, &ImageView::unhovered, this, [this] {
        cursor_.reset();
        view_->setGuide(std::nullopt);
        positionLabel_->clear();
    });
    connect(view_, &ImageView::clicked, this, &VueWindow::clicked);
    connect(view_, &ImageView::handleMoved, this, &VueWindow::handleMoved);
    connect(view_, &ImageView::handleRemoveRequested, this, &VueWindow::removeHandle);
    connect(view_, &ImageView::rectangleDragged, this, &VueWindow::applyZone);
    // On the image only, so Backspace still edits the Levels fields.
    auto* undoPointKey = new QShortcut(QKeySequence(Qt::Key_Backspace), view_);
    undoPointKey->setContext(Qt::WidgetShortcut);
    connect(undoPointKey, &QShortcut::activated, this, &VueWindow::undoPoint);
    connect(details, &QToolButton::toggled, this, [details, info](bool open) {
        info->setVisible(open);
        details->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(principale_, &QPushButton::clicked, this, &VueWindow::flagPrincipale);
    connect(exportCopy, &QPushButton::clicked, this, &VueWindow::exportCopy);
    connect(deleteVue, &QPushButton::clicked, this, &VueWindow::deleteRequested);

    showSaved();
    showSavedLevels();
    refreshTreatments();
    refreshPrincipale();
    refreshHistory();
    refreshTool();
}

QWidget* VueWindow::buildImagePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Levels on the photo's histogram (#93); the spin boxes for exact values.
    levels_ = new LevelsWidget;
    levels_->setObjectName(QStringLiteral("levelsWidget"));
    levels_->setHistogram(levelsHistogram_);
    levels_->setToolTip(tr("Glissez les poignées noire et blanche. Ce qui est hors de l'intervalle devient noir ou "
                           "blanc."));
    black_ = new QSpinBox;
    black_->setObjectName(QStringLiteral("levelsBlack"));
    black_->setRange(0, 254);
    white_ = new QSpinBox;
    white_->setObjectName(QStringLiteral("levelsWhite"));
    white_->setRange(1, 255);
    auto* stretch = new QPushButton(tr("Auto"));
    stretch->setObjectName(QStringLiteral("autoLevels"));
    stretch->setToolTip(tr("Étirer les niveaux : 0,5 % des valeurs deviennent noires, 0,5 % blanches."));
    auto* fields = new QHBoxLayout;
    fields->setSpacing(6);
    fields->addWidget(new QLabel(tr("Noir")));
    fields->addWidget(black_, 1);
    fields->addSpacing(8);
    fields->addWidget(new QLabel(tr("Blanc")));
    fields->addWidget(white_, 1);
    fields->addSpacing(8);
    fields->addWidget(stretch);
    levelsState_ = new QLabel(tr("Aperçu non enregistré"));
    theme::setRole(levelsState_, "role", QStringLiteral("warning"));
    applyLevels_ = new QPushButton(tr("Appliquer"));
    applyLevels_->setObjectName(QStringLiteral("applyLevels"));
    theme::setRole(applyLevels_, "variant", QStringLiteral("primary"));
    ignoreLevels_ = new QPushButton(tr("Ignorer"));
    ignoreLevels_->setObjectName(QStringLiteral("ignoreLevels"));
    ignoreLevels_->setToolTip(tr("Revenir aux niveaux enregistrés."));
    auto* levelsButtons = new QHBoxLayout;
    levelsButtons->setSpacing(8);
    levelsButtons->addWidget(levelsState_, 1);
    levelsButtons->addWidget(ignoreLevels_);
    levelsButtons->addWidget(applyLevels_);
    levelsTimer_ = new QTimer(this);
    levelsTimer_->setSingleShot(true);
    levelsTimer_->setInterval(150);  // re-render once the handle or the spin box settles

    treatments_ = new QListWidget;
    treatments_->setMaximumHeight(120);
    auto* remove = new QPushButton(tr("Retirer le traitement choisi"));
    theme::setRole(remove, "variant", QStringLiteral("quiet"));
    echelle_ = new QPushButton(tr("Ajouter l'Échelle"));
    connect(remove, &QPushButton::clicked, this, &VueWindow::removeTreatment);
    connect(stretch, &QPushButton::clicked, this, &VueWindow::stretchLevels);

    layout->addWidget(sectionTitle(tr("Niveaux")));
    layout->addWidget(levels_);
    layout->addLayout(fields);
    layout->addLayout(levelsButtons);
    layout->addSpacing(12);
    layout->addWidget(sectionTitle(tr("Traitements")));
    layout->addWidget(treatments_);
    auto* treatmentButtons = new QHBoxLayout;
    treatmentButtons->setSpacing(8);
    treatmentButtons->addWidget(remove, 1);
    treatmentButtons->addWidget(echelle_, 1);
    layout->addLayout(treatmentButtons);
    layout->addStretch(1);
    return page;
}

QWidget* VueWindow::buildMeasuresPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    measureResult_ = new QLabel;
    measureResult_->setObjectName(QStringLiteral("measureResult"));
    measureResult_->setWordWrap(true);
    measureResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    showText(measureResult_, tr("Choisissez Longueur ou Dureté Vickers dans la barre d'outils."), "muted");
    auto* clearMarks = new QPushButton(tr("Effacer les mesures"));
    theme::setRole(clearMarks, "variant", QStringLiteral("quiet"));
    auto* help = new QLabel(keepColons(tr("Glisser un point jaune : le corriger. Clic droit dessus : supprimer la "
                                          "mesure. Retour arrière : annuler le dernier point. Échap : ranger l'outil."
                                          "\n\nZoom : molette, ou 0 (toute l'image), 1 (100 %), + et −.")));
    help->setWordWrap(true);
    theme::setRole(help, "role", QStringLiteral("muted"));
    connect(clearMarks, &QPushButton::clicked, this, [this] {
        cancelTool();
        marks_.clear();
        showText(measureResult_, tr("Mesures effacées."), "muted");
        redrawMarks();
    });

    layout->addWidget(sectionTitle(tr("Résultat")));
    layout->addWidget(measureResult_);
    layout->addWidget(clearMarks, 0, Qt::AlignLeft);
    layout->addSpacing(12);
    layout->addWidget(help);
    layout->addStretch(1);
    return page;
}

QWidget* VueWindow::buildPhasesPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    zoneLabel_ = new QLabel;
    zoneLabel_->setWordWrap(true);
    theme::setRole(zoneLabel_, "role", QStringLiteral("muted"));
    auto* wholeImage = new QPushButton(tr("Toute l'image"));
    wholeImage->setToolTip(tr("Analyser toute l'image, sans zone."));
    theme::setRole(wholeImage, "variant", QStringLiteral("quiet"));
    auto* zoneRow = new QHBoxLayout;
    zoneRow->addWidget(zoneLabel_, 1);
    zoneRow->addWidget(wholeImage);
    auto* calcul = new QPushButton(tr("CALCUL : pourcentage des constituants"));
    calcul->setObjectName(QStringLiteral("calculButton"));
    theme::setRole(calcul, "variant", QStringLiteral("primary"));
    phaseResult_ = new QLabel;
    phaseResult_->setObjectName(QStringLiteral("phaseResult"));
    phaseResult_->setWordWrap(true);
    phaseResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    histogram_ = new HistogramWidget;
    phaseMap_ = new QPushButton(tr("Afficher la carte des phases"));
    phaseMap_->setEnabled(false);
    connect(wholeImage, &QPushButton::clicked, this, [this] { setZone(std::nullopt); });
    connect(calcul, &QPushButton::clicked, this, &VueWindow::analysePhases);
    connect(phaseMap_, &QPushButton::clicked, this, &VueWindow::showPhaseMap);
    // The zone of the phase map saved with this Vue, so CALCUL counts the
    // same pixels as last time.
    if (const auto index = indexOf("phase_overlay")) {
        zone_ = dynamic_cast<const PhaseOverlayTreatment&>(vue_.treatment(*index)).zone();
    }
    refreshZone();

    layout->addLayout(zoneRow);
    layout->addWidget(calcul);
    layout->addWidget(phaseResult_);
    layout->addWidget(histogram_);
    layout->addWidget(phaseMap_);
    layout->addStretch(1);
    return page;
}

QImage VueWindow::render(std::optional<std::pair<int, int>> levels) const {
    cv::Mat image = vue_.original();
    const auto own = indexOf("levels");
    std::unique_ptr<LevelsTreatment> preview;
    if (levels) preview = std::make_unique<LevelsTreatment>(levels->first, levels->second);
    if (preview && !own) image = preview->apply(image);  // new Levels go first
    for (std::size_t i = 0; i < vue_.treatmentCount(); ++i) {
        image = (preview && own && *own == i) ? preview->apply(image) : vue_.treatment(i).apply(image);
    }
    return toQImage(image);
}

void VueWindow::showSaved() {
    view_->setImage(render());
}

std::optional<std::size_t> VueWindow::indexOf(const std::string& type) const {
    for (std::size_t i = 0; i < vue_.treatmentCount(); ++i) {
        if (vue_.treatment(i).type() == type) return i;
    }
    return std::nullopt;
}

template <typename Edit>
void VueWindow::edit(Edit change) {
    const nlohmann::json before = vue_.treatmentsJson();
    const auto levelsBefore = savedLevels();
    try {
        change();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Traitement non enregistré"), QString::fromStdString(e.what()));
    }
    if (vue_.treatmentsJson() != before) {
        undoStack_.push_back(before);
        if (undoStack_.size() > kHistory) undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }
    pipelineChanged(levelsBefore);
}

void VueWindow::stepHistory(bool back) {
    std::vector<nlohmann::json>& from = back ? undoStack_ : redoStack_;
    std::vector<nlohmann::json>& to = back ? redoStack_ : undoStack_;
    if (from.empty()) return;
    const auto levelsBefore = savedLevels();
    nlohmann::json current = vue_.treatmentsJson();
    try {
        vue_.setTreatments(from.back());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, back ? tr("Annulation impossible") : tr("Rétablissement impossible"),
                             QString::fromStdString(e.what()));
        return;
    }
    from.pop_back();
    to.push_back(std::move(current));
    pipelineChanged(levelsBefore);
    statusBar()->showMessage(back ? tr("Modification annulée.") : tr("Modification rétablie."), 3000);
}

void VueWindow::pipelineChanged(std::pair<int, int> levelsBefore) {
    // Levels set, removed or undone: the fields follow. A preview pending
    // over another edit stays.
    if (savedLevels() != levelsBefore) showSavedLevels();
    // What is saved now, the change or the pipeline as it was: rendering it
    // can throw too (a Treatment refusing this image).
    guardSlot(this, tr("Affichage impossible"), [this] {
        if (levelsPending()) {
            previewLevels();
        } else {
            showSaved();
        }
        refreshTreatments();
    });
    refreshLevels();
    refreshHistory();
}

void VueWindow::refreshHistory() {
    undo_->setEnabled(!undoStack_.empty());
    redo_->setEnabled(!redoStack_.empty());
}

void VueWindow::refreshTreatments() {
    treatments_->clear();
    for (std::size_t i = 0; i < vue_.treatmentCount(); ++i) treatments_->addItem(describe(vue_.treatment(i)));
    echelle_->setEnabled(vue_.calibration().has_value() && !indexOf("echelle"));
}

void VueWindow::refreshPrincipale() {
    bool flagged = false;
    try {
        flagged = store_.principale(location_) == vue_.imagePath();
    } catch (const std::exception&) {
    }
    // Flagged, the disabled button reads as a copper badge, not as greyed out.
    principale_->setText(flagged ? tr("★ Vue principale") : tr("☆ Vue principale"));
    principale_->setToolTip(flagged ? tr("La Vue imprimée sur la Fiche du TP.")
                                    : tr("Choisir comme Vue principale : celle imprimée sur la Fiche du TP."));
    principale_->setEnabled(!flagged);
    theme::setRole(principale_, "flagged", flagged ? QStringLiteral("true") : QStringLiteral("false"));
}

std::pair<int, int> VueWindow::savedLevels() const {
    if (const auto index = indexOf("levels")) {
        const auto& levels = dynamic_cast<const LevelsTreatment&>(vue_.treatment(*index));
        return {levels.blackPoint(), levels.whitePoint()};
    }
    return {0, 255};
}

bool VueWindow::levelsPending() const {
    return std::make_pair(black_->value(), white_->value()) != savedLevels();
}

void VueWindow::showSavedLevels() {
    const auto [black, white] = savedLevels();
    levelsTimer_->stop();
    setLevelsFields(black, white);
    refreshLevels();
}

void VueWindow::setLevelsFields(int black, int white) {
    const QSignalBlocker blockBlack(black_);
    const QSignalBlocker blockWhite(white_);
    black_->setValue(black);
    white_->setValue(white);
    levels_->setLevels(black, white);
}

void VueWindow::refreshLevels() {
    const bool pending = levelsPending();
    levelsState_->setVisible(pending);
    applyLevels_->setEnabled(pending);
    ignoreLevels_->setEnabled(pending);
    levels_->setLevels(black_->value(), white_->value());
}

void VueWindow::previewLevels() {
    guardSlot(this, tr("Aperçu des niveaux"),
              [this] { view_->setImage(render(std::make_pair(black_->value(), white_->value()))); });
}

void VueWindow::stretchLevels() {
    const auto [black, white] = msapp::autoLevels(levelsHistogram_);
    setLevelsFields(black, white);
    levelsTimer_->stop();
    previewLevels();
    refreshLevels();
}

void VueWindow::applyLevels() {
    levelsTimer_->stop();
    const int black = black_->value();
    const int white = white_->value();
    edit([&] {
        auto levels = std::make_unique<LevelsTreatment>(black, white);
        if (const auto index = indexOf("levels")) {
            vue_.replaceTreatment(*index, std::move(levels));
        } else {
            vue_.insertTreatment(0, std::move(levels));  // before the Échelle: the bar keeps its contrast
        }
    });
}

void VueWindow::discardPendingChanges() {
    discardOnClose_ = true;
}

void VueWindow::closeEvent(QCloseEvent* event) {
    if (discardOnClose_ || !levelsPending()) {
        QMainWindow::closeEvent(event);
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Niveaux non enregistrés"), tr("Appliquer les niveaux avant de fermer ?"),
                    QMessageBox::NoButton, this);
    QPushButton* apply = box.addButton(tr("Appliquer"), QMessageBox::AcceptRole);
    QPushButton* drop = box.addButton(tr("Ne pas appliquer"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(apply);
    box.exec();
    if (box.clickedButton() == apply) {
        applyLevels();
        if (levelsPending()) {  // not saved: the warning said why; stay open
            event->ignore();
            return;
        }
    } else if (box.clickedButton() != drop) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void VueWindow::addEchelle() {
    if (!vue_.calibration()) return;
    edit([&] { vue_.addTreatment(std::make_unique<EchelleTreatment>(*vue_.calibration())); });
}

void VueWindow::removeTreatment() {
    const int row = treatments_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("Traitements"), tr("Choisissez d'abord un traitement dans la liste."));
        return;
    }
    edit([&] { vue_.removeTreatment(static_cast<std::size_t>(row)); });
}

void VueWindow::startTool(Tool tool) {
    const bool sameTool = tool_ == tool;
    cancelTool();  // drops what the previous tool left half-done
    if (sameTool) return;  // its button pressed again: put it away
    tool_ = tool;
    if (tool != Tool::Zone) marks_.start(tool == Tool::Vickers ? MeasureTool::Vickers : MeasureTool::Length);
    // The tab where the results show.
    tabs_->setCurrentWidget(tool == Tool::Zone ? phasesPage_ : measuresPage_);
    refreshTool();
}

void VueWindow::cancelTool() {
    marks_.cancel();
    tool_.reset();
    clicks_.clear();
    refreshZone();
    refreshTool();
    redrawMarks();
}

void VueWindow::refreshTool() {
    lengthTool_->setChecked(tool_ == Tool::Length);
    vickersTool_->setChecked(tool_ == Tool::Vickers);
    zoneTool_->setChecked(tool_ == Tool::Zone);
    view_->setRectangleMode(tool_ == Tool::Zone);  // a drag draws the zone instead of panning
    view_->setLoupe(tool_ == Tool::Length || tool_ == Tool::Vickers);
    // What to click next, over the photo, where the eyes are.
    QString prompt;
    if (tool_ == Tool::Zone) {
        prompt = clicks_.empty() ? tr("Zone : glissez un rectangle sur l'image · Échap : annuler")
                                 : tr("Zone : cliquez le coin opposé · Échap : annuler");
    } else if (tool_) {
        const std::size_t done = marks_.pendingPoints();
        if (*tool_ == Tool::Length) {
            prompt = done == 0 ? tr("Longueur — point 1/2 : cliquez une extrémité · Échap : terminer")
                               : tr("Longueur — point 2/2 : cliquez l'autre extrémité · Échap : terminer");
        } else {
            static const char* const steps[] = {
                QT_TR_NOOP("Vickers — point 1/4 : un sommet de la première diagonale · Échap : terminer"),
                QT_TR_NOOP("Vickers — point 2/4 : le sommet opposé de la première diagonale · Échap : terminer"),
                QT_TR_NOOP("Vickers — point 3/4 : un sommet de la seconde diagonale · Échap : terminer"),
                QT_TR_NOOP("Vickers — point 4/4 : le sommet opposé de la seconde diagonale · Échap : terminer"),
            };
            prompt = tr(steps[std::min<std::size_t>(done, 3)]);
        }
    }
    view_->setPrompt(keepColons(prompt));
    refreshGuide();
}

void VueWindow::hovered(QPointF point) {
    cursor_ = point;
    refreshGuide();
    QString position = tr("x %1  y %2 px").arg(static_cast<int>(point.x())).arg(static_cast<int>(point.y()));
    if (const auto& calibration = vue_.calibration()) {
        position += tr("  ·  %1 × %2 µm")
                        .arg(point.x() / calibration->pixelsPerMicron, 0, 'f', 1)
                        .arg(point.y() / calibration->pixelsPerMicron, 0, 'f', 1);
    }
    positionLabel_->setText(position);
}

void VueWindow::refreshGuide() {
    // From the first point of a length or of a diagonal to the cursor, with
    // the length it would read (#90).
    std::optional<QLineF> line;
    QString label;
    const std::size_t done = marks_.pendingPoints();
    if (cursor_ && (tool_ == Tool::Length || tool_ == Tool::Vickers) && (done == 1 || done == 3) &&
        !marks_.marks().empty()) {
        const cv::Point2d from = marks_.marks().back().points.back();
        line = QLineF(toViewPoint(from), *cursor_);
        try {
            label = micronsLabel(Measurement(from, toImagePoint(*cursor_), vue_).label());
        } catch (const std::exception&) {
            // The cursor on the point itself: no length yet.
        }
    }
    view_->setGuide(line, label);
}

void VueWindow::clicked(QPointF point) {
    if (!tool_) return;
    if (*tool_ == Tool::Zone) {
        // Two clicks at opposite corners still work, besides dragging.
        clicks_.push_back(point);
        if (clicks_.size() < 2) {
            refreshTool();
            redrawMarks();
            return;
        }
        applyZone(QRectF(clicks_[0], clicks_[1]));
        return;
    }
    // A finished reading is shown; the tool stays out for the next one.
    if (marks_.click(toImagePoint(point))) showResult(marks_.marks()[*marks_.current()]);
    refreshTool();
    redrawMarks();
}

void VueWindow::applyZone(QRectF corners) {
    if (tool_ != Tool::Zone) return;
    corners = corners.normalized();
    const cv::Size size = vue_.original().size();
    const cv::Rect zone = cv::Rect(cv::Point(qRound(corners.left()), qRound(corners.top())),
                                   cv::Point(qRound(corners.right()), qRound(corners.bottom()))) &
                          cv::Rect(cv::Point(0, 0), size);
    cancelTool();
    if (phaseZoneProblem(zone, size)) {
        QMessageBox::warning(this, tr("Zone refusée"),
                             tr("Zone trop petite pour l'analyse : il faut au moins %1 × %1 pixels.")
                                 .arg(minPhaseZoneSide(size)));
        return;
    }
    setZone(zone);
}

void VueWindow::handleMoved(int index, QPointF point) {
    if (marks_.moveHandle(index, toImagePoint(point))) showResult(marks_.marks()[*marks_.current()]);
    redrawMarks();
}

void VueWindow::removeHandle(int index) {
    if (!marks_.markPointAt(index)) return;
    const bool shown = marks_.current().has_value();
    marks_.removeHandle(index);
    if (shown && !marks_.current()) showText(measureResult_, tr("Mesure supprimée."), "muted");
    refreshTool();
    redrawMarks();
}

void VueWindow::undoPoint() {
    if (tool_ == Tool::Zone) {
        if (!clicks_.empty()) clicks_.pop_back();
    } else if (tool_) {
        // Reopening the reading just finished takes its value away.
        const bool shown = marks_.current().has_value();
        marks_.undo();
        if (shown && !marks_.current()) measureResult_->clear();
    } else {
        return;
    }
    refreshTool();
    redrawMarks();
}

void VueWindow::showResult(const Mark& mark) {
    const auto at = [&](std::size_t i) { return mark.points[i]; };
    try {
        if (mark.tool == MeasureTool::Length) {
            const Measurement length(at(0), at(1), vue_);
            showText(measureResult_,
                     tr("Longueur : %1 (%2 px)").arg(micronsLabel(length.label())).arg(length.pixelDistance(), 0, 'f', 1),
                     "readout");
        } else {
            const VickersResult hv =
                vickersHardness(Measurement(at(0), at(1), vue_), Measurement(at(2), at(3), vue_),
                                load_->currentData().toDouble());
            showText(measureResult_,
                     tr("%1\nDiagonales : %2 µm et %3 µm (moyenne %4 µm)")
                         .arg(QString::fromStdString(hv.label()))
                         .arg(hv.diagonal1Microns, 0, 'f', 2)
                         .arg(hv.diagonal2Microns, 0, 'f', 2)
                         .arg(hv.meanDiagonalMicrons, 0, 'f', 2),
                     "readout");
        }
    } catch (const std::exception& e) {
        // e.g. two points dragged onto each other: say so, keep the Mark.
        showText(measureResult_, tr("Mesure impossible : %1").arg(QString::fromStdString(e.what())), "muted");
    }
}

void VueWindow::redrawMarks() {
    view_->clearMarks();
    if (zone_) {
        const QPointF topLeft(zone_->x, zone_->y);
        const QPointF topRight(zone_->x + zone_->width, zone_->y);
        const QPointF bottomRight(zone_->x + zone_->width, zone_->y + zone_->height);
        const QPointF bottomLeft(zone_->x, zone_->y + zone_->height);
        view_->addLine(topLeft, topRight);
        view_->addLine(topRight, bottomRight);
        view_->addLine(bottomRight, bottomLeft);
        view_->addLine(bottomLeft, topLeft);
    }
    for (const QPointF& corner : clicks_) view_->addPoint(corner);

    std::vector<QPointF> handles;
    for (const Mark& mark : marks_.marks()) {
        std::vector<QPointF> p;
        for (const cv::Point2d& point : mark.points) p.push_back(toViewPoint(point));
        handles.insert(handles.end(), p.begin(), p.end());
        if (p.size() >= 2) view_->addLine(p[0], p[1]);
        if (p.size() == 4) view_->addLine(p[2], p[3]);
        if (!mark.complete()) continue;
        const auto at = [&](std::size_t i) { return mark.points[i]; };
        try {
            const Measurement first(at(0), at(1), vue_);
            view_->addLabel((p[0] + p[1]) / 2, micronsLabel(first.label()));
            if (mark.tool == MeasureTool::Vickers) {
                const Measurement second(at(2), at(3), vue_);
                view_->addLabel((p[2] + p[3]) / 2, micronsLabel(second.label()));
                const VickersResult hv = vickersHardness(first, second, load_->currentData().toDouble());
                view_->addLabel(p[3], QString::fromStdString(hv.label()));
            }
        } catch (const std::exception&) {
            // No value to show (showResult says why); the lines stay.
        }
    }
    view_->setHandles(handles);
    refreshGuide();  // clearMarks() took it away
}

void VueWindow::analysePhases() {
    try {
        const WaitCursor wait;
        // On the photo as captured: Levels would shift the grey levels, and the
        // Échelle's bar and the phase map would be counted as phases. A 4K
        // photo is analysed at the legacy camera's scale (ADR 0021).
        phases_ = analyzeVuePhases(vue_.original(), zone_);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Analyse de phases"), QString::fromStdString(e.what()));
        return;
    }
    if (!phases_->threshold.found) {
        // The legacy app printed its default 127 ± 42 and a meaningless %.
        phases_.reset();
        histogram_->clear();
        phaseMap_->setEnabled(false);
        phaseResult_->setText(tr("Seuil introuvable : contours trop flous entre les phases. Aucun pourcentage."));
        return;
    }
    // The two percentages large, how far to trust them under.
    const PhaseFractions& f = phases_->fractions;
    const auto row = [](const QString& name, const QString& value, bool large) {
        return QStringLiteral("<tr><td style=\"color:%1\">%2</td><td align=\"right\" style=\"%3\">%4</td></tr>")
            .arg(theme::textMuted.name(), name.toHtmlEscaped(),
                 large ? QStringLiteral("font-size:13pt; font-weight:600") : QString(), value.toHtmlEscaped());
    };
    phaseResult_->setText(QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"2\">") +
                          row(tr("Phase sombre"), QStringLiteral("%1 %").arg(f.darkPercent, 0, 'f', 1), true) +
                          row(tr("Phase claire"), QStringLiteral("%1 %").arg(f.lightPercent, 0, 'f', 1), true) +
                          row(tr("Incertitude"), QStringLiteral("± %1 %").arg(f.errorPercent, 0, 'f', 1), false) +
                          row(tr("Seuil"), QStringLiteral("%1 ± %2")
                                               .arg(phases_->threshold.threshold)
                                               .arg(phases_->threshold.tolerance),
                              false) +
                          QStringLiteral("</table>"));
    histogram_->setHistogram(phases_->threshold.histogram, phases_->threshold.threshold, phases_->threshold.tolerance);
    phaseMap_->setEnabled(true);
}

void VueWindow::setZone(std::optional<cv::Rect> zone) {
    zone_ = zone;
    // The percentages and the map belonged to the previous zone.
    phases_.reset();
    phaseResult_->setText(zone_ ? tr("Lancez CALCUL sur cette zone.") : tr("Lancez CALCUL sur toute l'image."));
    phaseMap_->setEnabled(false);
    refreshZone();
    redrawMarks();
}

void VueWindow::refreshZone() {
    if (!zone_) {
        zoneLabel_->setText(tr("Zone analysée : toute l'image."));
        return;
    }
    const cv::Size size = vue_.original().size();
    zoneLabel_->setText(tr("Zone analysée : %1×%2 px, %3 % de l'image.")
                            .arg(zone_->width)
                            .arg(zone_->height)
                            .arg(100.0 * zone_->area() / (static_cast<double>(size.width) * size.height), 0, 'f', 0));
}

void VueWindow::showPhaseMap() {
    if (!phases_) return;
    const int threshold = phases_->threshold.threshold;
    edit([&] {
        auto overlay = std::make_unique<PhaseOverlayTreatment>(threshold, zone_);
        if (const auto index = indexOf("phase_overlay")) {
            vue_.replaceTreatment(*index, std::move(overlay));
        } else if (const auto echelle = indexOf("echelle")) {
            vue_.insertTreatment(*echelle, std::move(overlay));  // the bar stays readable on top
        } else {
            vue_.addTreatment(std::move(overlay));
        }
    });
}

void VueWindow::exportCopy() {
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(toQString(vue_.imagePath().stem()) + QStringLiteral(".png"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter une copie de la Vue"), suggested,
                                                      tr("PNG (*.png);;TIFF (*.tif *.tiff);;JPEG (*.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    try {
        const WaitCursor wait;
        vue_.exportRendered(toPath(path));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Export impossible"), QString::fromStdString(e.what()));
        return;
    }
    statusBar()->showMessage(tr("Copie exportée : %1").arg(path), 10000);
}

void VueWindow::flagPrincipale() {
    try {
        store_.setPrincipale(location_, vue_.imagePath());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Vue principale"), QString::fromStdString(e.what()));
        return;
    }
    refreshPrincipale();
    emit principaleChanged();
}

} // namespace msapp
