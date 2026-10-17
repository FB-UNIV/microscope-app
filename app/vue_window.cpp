#include "vue_window.hpp"

#include "echelle_treatment.hpp"
#include "histogram_widget.hpp"
#include "image_view.hpp"
#include "levels_treatment.hpp"
#include "measurement.hpp"
#include "phase_overlay_treatment.hpp"
#include "qt_image.hpp"
#include "vickers.hpp"
#include "wait_cursor.hpp"

#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <exception>
#include <memory>
#include <utility>

namespace msapp {

namespace {

QString describe(const Treatment& treatment) {
    if (const auto* levels = dynamic_cast<const LevelsTreatment*>(&treatment)) {
        return VueWindow::tr("Niveaux : noir %1, blanc %2").arg(levels->blackPoint()).arg(levels->whitePoint());
    }
    if (const auto* echelle = dynamic_cast<const EchelleTreatment*>(&treatment)) {
        return VueWindow::tr("Échelle (%1)").arg(QString::fromStdString(echelle->calibration().grossissement));
    }
    if (const auto* overlay = dynamic_cast<const PhaseOverlayTreatment*>(&treatment)) {
        return VueWindow::tr("Carte des phases (seuil %1)").arg(overlay->threshold());
    }
    return QString::fromStdString(treatment.type());
}

// Measurement labels are plain ASCII ("2.5 um") for the Échelle's Hershey
// font; the window can show the real unit.
QString micronsLabel(const std::string& label) {
    return QString::fromStdString(label).replace(QStringLiteral(" um"), QStringLiteral(" µm"));
}

QString captureText(const Vue& vue) {
    QStringList lines;
    lines << VueWindow::tr("Fichier : %1").arg(toQString(vue.imagePath().filename()));
    lines << VueWindow::tr("Image : %1x%2").arg(vue.original().cols).arg(vue.original().rows);
    if (const auto& calibration = vue.calibration()) {
        lines << VueWindow::tr("Calibration : %1, %2 px/µm%3")
                     .arg(QString::fromStdString(calibration->grossissement))
                     .arg(calibration->pixelsPerMicron, 0, 'f', 4)
                     .arg(calibration->isDerived()
                              ? VueWindow::tr(" (déduite de %1)").arg(QString::fromStdString(calibration->derivedFrom))
                              : QString());
    } else {
        lines << VueWindow::tr("Sans Calibration : ni Échelle ni mesure.");
    }
    if (const auto& info = vue.captureInfo()) {
        lines << VueWindow::tr("Exposition : %1").arg(info->exposureMs ? QStringLiteral("%1 ms").arg(*info->exposureMs)
                                                                        : VueWindow::tr("automatique ou inconnue"));
        lines << VueWindow::tr("Température de couleur : %1")
                     .arg(info->whiteBalanceKelvin ? QStringLiteral("%1 K").arg(*info->whiteBalanceKelvin)
                                                   : VueWindow::tr("automatique ou inconnue"));
        lines << VueWindow::tr("Référence blanche : R %1, V %2, B %3")
                     .arg(info->whiteReference.red, 0, 'f', 3)
                     .arg(info->whiteReference.green, 0, 'f', 3)
                     .arg(info->whiteReference.blue, 0, 'f', 3);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

VueWindow::VueWindow(Vue vue, VueStore store, VueLocation location, QWidget* parent)
    : QMainWindow(parent), vue_(std::move(vue)), store_(std::move(store)), location_(std::move(location)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Vue — %1").arg(toQString(vue_.imagePath().filename())));
    resize(1400, 900);

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setMargin(8);
    banner_->setStyleSheet(QStringLiteral("background: #f8d7da; color: #58151c;"));
    // ADR 0003/0015: what the sidecar lost, while the photo itself loaded.
    if (const auto& error = vue_.sidecarError()) {
        banner_->setText(tr("⚠ Une partie des traitements de cette Vue n'a pas pu être relue ; le fichier est mis de "
                            "côté à la prochaine modification.\n%1")
                             .arg(QString::fromStdString(*error)));
    } else {
        banner_->hide();
    }
    view_ = new ImageView;
    auto* central = new QWidget;
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(banner_);
    centralLayout->addWidget(view_, 1);
    setCentralWidget(central);

    // Capture details.
    auto* info = new QLabel(captureText(vue_));
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Treatments.
    treatments_ = new QListWidget;
    treatments_->setMaximumHeight(110);
    auto* remove = new QPushButton(tr("Retirer le traitement choisi"));
    black_ = new QSpinBox;
    black_->setRange(0, 254);
    white_ = new QSpinBox;
    white_->setRange(1, 255);
    if (const auto index = indexOf("levels")) {
        const auto& levels = dynamic_cast<const LevelsTreatment&>(vue_.treatment(*index));
        black_->setValue(levels.blackPoint());
        white_->setValue(levels.whitePoint());
    } else {
        white_->setValue(255);
    }
    auto* applyLevels = new QPushButton(tr("Appliquer les niveaux"));
    echelle_ = new QPushButton(tr("Ajouter l'Échelle"));
    auto* levelsForm = new QFormLayout;
    levelsForm->addRow(tr("Noir"), black_);
    levelsForm->addRow(tr("Blanc"), white_);
    auto* treatmentsBox = new QGroupBox(tr("Traitements"));
    auto* treatmentsLayout = new QVBoxLayout(treatmentsBox);
    treatmentsLayout->addWidget(treatments_);
    treatmentsLayout->addWidget(remove);
    treatmentsLayout->addLayout(levelsForm);
    treatmentsLayout->addWidget(applyLevels);
    treatmentsLayout->addWidget(echelle_);
    levelsTimer_ = new QTimer(this);
    levelsTimer_->setSingleShot(true);
    levelsTimer_->setInterval(150);  // re-render once the spin box settles

    // Measurement and Dureté Vickers.
    lengthTool_ = new QPushButton(tr("Mesurer une longueur"));
    vickersTool_ = new QPushButton(tr("Dureté Vickers"));
    load_ = new QComboBox;
    for (const double kgf : {0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 30.0}) {
        load_->addItem(QStringLiteral("HV%1 (%1 kgf)").arg(kgf), kgf);
    }
    load_->setCurrentIndex(load_->findData(kLegacyVickersLoadKgf));
    measureResult_ = new QLabel;
    measureResult_->setWordWrap(true);
    measureResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* clearMarks = new QPushButton(tr("Effacer les mesures"));
    auto* measureBox = new QGroupBox(tr("Mesures"));
    auto* measureLayout = new QVBoxLayout(measureBox);
    measureLayout->addWidget(lengthTool_);
    auto* vickersRow = new QHBoxLayout;
    vickersRow->addWidget(vickersTool_);
    vickersRow->addWidget(load_);
    measureLayout->addLayout(vickersRow);
    measureLayout->addWidget(measureResult_);
    measureLayout->addWidget(clearMarks);
    if (!vue_.calibration()) {
        const QString why = tr("Cette Vue n'a pas de Calibration : aucune mesure n'est possible (ADR 0015).");
        lengthTool_->setEnabled(false);
        vickersTool_->setEnabled(false);
        lengthTool_->setToolTip(why);
        vickersTool_->setToolTip(why);
        measureResult_->setText(why);
    }

    // Phase analysis.
    auto* calcul = new QPushButton(tr("CALCUL : pourcentage des constituants"));
    phaseResult_ = new QLabel;
    phaseResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    histogram_ = new HistogramWidget;
    phaseMap_ = new QPushButton(tr("Afficher la carte des phases"));
    phaseMap_->setEnabled(false);
    auto* phaseBox = new QGroupBox(tr("Analyse de phases"));
    auto* phaseLayout = new QVBoxLayout(phaseBox);
    phaseLayout->addWidget(calcul);
    phaseLayout->addWidget(phaseResult_);
    phaseLayout->addWidget(histogram_);
    phaseLayout->addWidget(phaseMap_);

    principale_ = new QPushButton;
    auto* exportCopy = new QPushButton(tr("Exporter une copie (traitements appliqués)…"));

    auto* panel = new QWidget;
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(info);
    panelLayout->addWidget(principale_);
    panelLayout->addWidget(exportCopy);
    panelLayout->addWidget(treatmentsBox);
    panelLayout->addWidget(measureBox);
    panelLayout->addWidget(phaseBox);
    panelLayout->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    auto* dock = new QDockWidget(tr("Vue"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(scroll);
    dock->setMinimumWidth(340);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(remove, &QPushButton::clicked, this, &VueWindow::removeTreatment);
    connect(black_, &QSpinBox::valueChanged, this, [this](int value) {
        if (white_->value() <= value) white_->setValue(value + 1);
        levelsTimer_->start();
    });
    connect(white_, &QSpinBox::valueChanged, this, [this](int value) {
        if (black_->value() >= value) black_->setValue(value - 1);
        levelsTimer_->start();
    });
    connect(levelsTimer_, &QTimer::timeout, this, &VueWindow::previewLevels);
    connect(applyLevels, &QPushButton::clicked, this, &VueWindow::applyLevels);
    connect(echelle_, &QPushButton::clicked, this, &VueWindow::addEchelle);
    connect(lengthTool_, &QPushButton::clicked, this, [this] { startTool(Tool::Length); });
    connect(vickersTool_, &QPushButton::clicked, this, [this] { startTool(Tool::Vickers); });
    connect(clearMarks, &QPushButton::clicked, this, [this] {
        tool_.reset();
        clicks_.clear();
        view_->clearMarks();
        measureResult_->clear();
    });
    connect(view_, &ImageView::clicked, this, &VueWindow::clicked);
    connect(calcul, &QPushButton::clicked, this, &VueWindow::analysePhases);
    connect(phaseMap_, &QPushButton::clicked, this, &VueWindow::showPhaseMap);
    connect(principale_, &QPushButton::clicked, this, &VueWindow::flagPrincipale);
    connect(exportCopy, &QPushButton::clicked, this, &VueWindow::exportCopy);

    showSaved();
    refreshTreatments();
    refreshPrincipale();
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
    try {
        change();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Traitement non enregistré"), QString::fromStdString(e.what()));
    }
    showSaved();
    refreshTreatments();
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
    principale_->setText(flagged ? tr("★ Vue principale de l'Échantillon") : tr("☆ Choisir comme Vue principale"));
    principale_->setEnabled(!flagged);
}

void VueWindow::previewLevels() {
    view_->setImage(render(std::make_pair(black_->value(), white_->value())));
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
    tool_ = tool;
    clicks_.clear();
    measureResult_->setText(tool == Tool::Length
                                ? tr("Cliquez les deux extrémités de la longueur.")
                                : tr("Cliquez les deux extrémités de la première diagonale de l'empreinte, puis de "
                                     "la seconde."));
}

void VueWindow::clicked(QPointF point) {
    if (!tool_) return;
    view_->addPoint(point);
    clicks_.push_back(point);
    const auto at = [&](std::size_t i) { return cv::Point2d(clicks_[i].x(), clicks_[i].y()); };
    try {
        if (*tool_ == Tool::Length && clicks_.size() == 2) {
            const Measurement length(at(0), at(1), vue_);
            view_->addLine(clicks_[0], clicks_[1]);
            view_->addLabel((clicks_[0] + clicks_[1]) / 2, micronsLabel(length.label()));
            measureResult_->setText(tr("Longueur : %1 (%2 px)")
                                        .arg(micronsLabel(length.label()))
                                        .arg(length.pixelDistance(), 0, 'f', 1));
            clicks_.clear();
        } else if (*tool_ == Tool::Vickers && clicks_.size() == 2) {
            view_->addLine(clicks_[0], clicks_[1]);
            measureResult_->setText(tr("Cliquez maintenant les deux extrémités de la seconde diagonale."));
        } else if (*tool_ == Tool::Vickers && clicks_.size() == 4) {
            view_->addLine(clicks_[2], clicks_[3]);
            const Measurement d1(at(0), at(1), vue_);
            const Measurement d2(at(2), at(3), vue_);
            const VickersResult hv = vickersHardness(d1, d2, load_->currentData().toDouble());
            view_->addLabel(clicks_[3], QString::fromStdString(hv.label()));
            measureResult_->setText(tr("%1\nDiagonales : %2 µm et %3 µm (moyenne %4 µm)")
                                        .arg(QString::fromStdString(hv.label()))
                                        .arg(hv.diagonal1Microns, 0, 'f', 2)
                                        .arg(hv.diagonal2Microns, 0, 'f', 2)
                                        .arg(hv.meanDiagonalMicrons, 0, 'f', 2));
            clicks_.clear();
        }
    } catch (const std::exception& e) {
        clicks_.clear();
        QMessageBox::warning(this, tr("Mesure refusée"), QString::fromStdString(e.what()));
    }
}

void VueWindow::analysePhases() {
    try {
        const WaitCursor wait;
        // On the photo as captured: Levels would shift the grey levels, and the
        // Échelle's bar and the phase map would be counted as phases.
        phases_ = analyzePhases(vue_.original());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Analyse de phases"), QString::fromStdString(e.what()));
        return;
    }
    const PhaseFractions& f = phases_->fractions;
    phaseResult_->setText(tr("Phase sombre : %1 %\nPhase claire : %2 %\nIncertitude : %3 %\nSeuil : %4 ± %5")
                              .arg(f.darkPercent, 0, 'f', 1)
                              .arg(f.lightPercent, 0, 'f', 1)
                              .arg(f.errorPercent, 0, 'f', 1)
                              .arg(phases_->threshold.threshold)
                              .arg(phases_->threshold.tolerance));
    histogram_->setHistogram(phases_->threshold.histogram, phases_->threshold.threshold, phases_->threshold.tolerance);
    phaseMap_->setEnabled(true);
}

void VueWindow::showPhaseMap() {
    if (!phases_) return;
    const int threshold = phases_->threshold.threshold;
    edit([&] {
        auto overlay = std::make_unique<PhaseOverlayTreatment>(threshold);
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
