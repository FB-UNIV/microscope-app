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
#include <QRectF>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
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

QString captureText(const Vue& vue) {
    QStringList lines;
    lines << VueWindow::tr("Fichier : %1").arg(toQString(vue.imagePath().filename()));
    lines << VueWindow::tr("Image : %1x%2").arg(vue.original().cols).arg(vue.original().rows);
    if (const auto& calibration = vue.calibration()) {
        lines << VueWindow::tr("Calibration : %1, %2 px/µm")
                     .arg(QString::fromStdString(calibration->grossissement))
                     .arg(calibration->pixelsPerMicron, 0, 'f', 4);
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

    // Measurement and Dureté Vickers. The active tool's button stays pressed;
    // pressing it again, or Échap, puts it away.
    lengthTool_ = new QPushButton(tr("Mesurer une longueur"));
    lengthTool_->setCheckable(true);
    vickersTool_ = new QPushButton(tr("Dureté Vickers"));
    vickersTool_->setCheckable(true);
    load_ = new QComboBox;
    for (const double kgf : {0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 3.0, 5.0, 10.0, 20.0, 30.0}) {
        load_->addItem(QStringLiteral("HV%1 (%1 kgf)").arg(kgf), kgf);
    }
    load_->setCurrentIndex(load_->findData(kLegacyVickersLoadKgf));
    toolPrompt_ = new QLabel;
    toolPrompt_->setWordWrap(true);
    toolPrompt_->setStyleSheet(QStringLiteral("background: #e7f1ff; padding: 6px; font-weight: bold;"));
    toolPrompt_->hide();
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
    measureLayout->addWidget(toolPrompt_);
    measureLayout->addWidget(measureResult_);
    measureLayout->addWidget(clearMarks);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    if (!vue_.calibration()) {
        const QString why = tr("Cette Vue n'a pas de Calibration : aucune mesure n'est possible (ADR 0015).");
        lengthTool_->setEnabled(false);
        vickersTool_->setEnabled(false);
        lengthTool_->setToolTip(why);
        vickersTool_->setToolTip(why);
        measureResult_->setText(why);
    }

    // Phase analysis, on the whole photo or on a zone of it (#55).
    zoneTool_ = new QPushButton(tr("Choisir une zone…"));
    zoneTool_->setCheckable(true);
    zoneTool_->setToolTip(tr("Deux clics aux coins opposés : l'analyse ne porte que sur cette zone, pour laisser de "
                              "côté poussières et inclusions. Elle doit couvrir au moins %1 % de l'image, pour "
                              "compter assez de grains.")
                               .arg(static_cast<int>(kMinPhaseZoneFraction * 100.0)));
    auto* wholeImage = new QPushButton(tr("Toute l'image"));
    zoneLabel_ = new QLabel;
    zoneLabel_->setWordWrap(true);
    auto* calcul = new QPushButton(tr("CALCUL : pourcentage des constituants"));
    phaseResult_ = new QLabel;
    phaseResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    histogram_ = new HistogramWidget;
    phaseMap_ = new QPushButton(tr("Afficher la carte des phases"));
    phaseMap_->setEnabled(false);
    auto* phaseBox = new QGroupBox(tr("Analyse de phases"));
    auto* phaseLayout = new QVBoxLayout(phaseBox);
    auto* zoneRow = new QHBoxLayout;
    zoneRow->addWidget(zoneTool_);
    zoneRow->addWidget(wholeImage);
    phaseLayout->addLayout(zoneRow);
    phaseLayout->addWidget(zoneLabel_);
    phaseLayout->addWidget(calcul);
    phaseLayout->addWidget(phaseResult_);
    phaseLayout->addWidget(histogram_);
    phaseLayout->addWidget(phaseMap_);
    // The zone of the phase map saved with this Vue, so CALCUL counts the
    // same pixels as last time.
    if (const auto index = indexOf("phase_overlay")) {
        zone_ = dynamic_cast<const PhaseOverlayTreatment&>(vue_.treatment(*index)).zone();
    }
    refreshZone();

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
        cancelTool();
        marks_.clear();
        current_.reset();
        measureResult_->clear();
        redrawMarks();
    });
    connect(escape, &QShortcut::activated, this, &VueWindow::cancelTool);
    // A Vickers value follows the load picked, for every indent shown.
    connect(load_, &QComboBox::currentIndexChanged, this, [this] {
        redrawMarks();
        if (current_) showResult(marks_[*current_]);
    });
    connect(view_, &ImageView::clicked, this, &VueWindow::clicked);
    connect(view_, &ImageView::handleMoved, this, &VueWindow::handleMoved);
    connect(view_, &ImageView::handleRemoveRequested, this, &VueWindow::removeHandle);
    connect(view_, &ImageView::rectangleDragged, this, &VueWindow::applyZone);
    // On the image only, so Backspace still edits the Levels fields.
    auto* undo = new QShortcut(QKeySequence(Qt::Key_Backspace), view_);
    undo->setContext(Qt::WidgetShortcut);
    connect(undo, &QShortcut::activated, this, &VueWindow::undoPoint);
    connect(zoneTool_, &QPushButton::clicked, this, [this] { startTool(Tool::Zone); });
    connect(wholeImage, &QPushButton::clicked, this, [this] { setZone(std::nullopt); });
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
    const bool sameTool = tool_ == tool;
    cancelTool();  // drops what the previous tool left half-done
    if (sameTool) return;  // its button pressed again: put it away
    tool_ = tool;
    if (tool != Tool::Zone) marks_.push_back(Mark{tool, {}});
    refreshTool();
}

void VueWindow::cancelTool() {
    if (!marks_.empty() && marks_.back().points.size() < pointsNeeded(marks_.back().tool)) marks_.pop_back();
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
    QString prompt;
    if (tool_ == Tool::Zone) {
        prompt = clicks_.empty() ? tr("Zone : glissez sur l'image pour tracer le rectangle à analyser.")
                                 : tr("Zone : cliquez le coin opposé.");
        zoneLabel_->setText(prompt + tr(" (Échap : annuler)"));
        prompt.clear();  // shown beside the zone buttons, not with the measurements
    } else if (tool_) {
        const std::size_t done = marks_.back().points.size();
        if (*tool_ == Tool::Length) {
            prompt = done == 0 ? tr("Longueur — point 1/2 : cliquez une extrémité.")
                               : tr("Longueur — point 2/2 : cliquez l'autre extrémité.");
        } else {
            static const char* const steps[] = {
                QT_TR_NOOP("Dureté Vickers — point 1/4 : un sommet de la première diagonale."),
                QT_TR_NOOP("Dureté Vickers — point 2/4 : le sommet opposé de la première diagonale."),
                QT_TR_NOOP("Dureté Vickers — point 3/4 : un sommet de la seconde diagonale."),
                QT_TR_NOOP("Dureté Vickers — point 4/4 : le sommet opposé de la seconde diagonale."),
            };
            prompt = tr(steps[std::min<std::size_t>(done, 3)]);
        }
        prompt += tr("\nGlisser un point jaune : le corriger. Clic droit dessus : supprimer la mesure. "
                     "Retour arrière : annuler le dernier point. Échap ou le même bouton : terminer.");
    }
    toolPrompt_->setText(prompt);
    toolPrompt_->setVisible(!prompt.isEmpty());
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
    Mark& mark = marks_.back();
    mark.points.push_back(point);
    if (mark.points.size() == pointsNeeded(mark.tool)) {
        current_ = marks_.size() - 1;
        showResult(mark);
        marks_.push_back(Mark{*tool_, {}});  // the tool stays out for the next one
    }
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
        const double share = 100.0 * zone.area() / (static_cast<double>(size.width) * size.height);
        QMessageBox::warning(this, tr("Zone refusée"),
                             tr("Cette zone couvre %1 % de l'image ; il en faut au moins %2 % pour compter assez "
                                "de grains.")
                                 .arg(share, 0, 'f', 0)
                                 .arg(static_cast<int>(kMinPhaseZoneFraction * 100.0)));
        return;
    }
    setZone(zone);
}

std::optional<std::pair<std::size_t, std::size_t>> VueWindow::markPointAt(int index) const {
    if (index < 0) return std::nullopt;
    std::size_t remaining = static_cast<std::size_t>(index);
    for (std::size_t i = 0; i < marks_.size(); ++i) {
        if (remaining < marks_[i].points.size()) return std::make_pair(i, remaining);
        remaining -= marks_[i].points.size();
    }
    return std::nullopt;
}

void VueWindow::handleMoved(int index, QPointF point) {
    if (const auto at = markPointAt(index)) {
        Mark& mark = marks_[at->first];
        mark.points[at->second] = point;
        if (mark.points.size() == pointsNeeded(mark.tool)) {
            current_ = at->first;
            showResult(mark);
        }
    }
    redrawMarks();
}

void VueWindow::removeHandle(int index) {
    const auto at = markPointAt(index);
    if (!at) return;
    const std::size_t i = at->first;
    const bool pending = tool_ && tool_ != Tool::Zone && i + 1 == marks_.size();
    if (pending) {
        marks_[i].points.erase(marks_[i].points.begin() + static_cast<std::ptrdiff_t>(at->second));
    } else {
        marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(i));
        if (current_ == i) {
            current_.reset();
            measureResult_->setText(tr("Mesure supprimée."));
        } else if (current_ && *current_ > i) {
            --*current_;
        }
    }
    refreshTool();
    redrawMarks();
}

void VueWindow::undoPoint() {
    if (tool_ == Tool::Zone) {
        if (!clicks_.empty()) clicks_.pop_back();
    } else if (tool_) {
        // Nothing clicked yet on the new reading: take back the one just finished.
        if (marks_.back().points.empty() && marks_.size() >= 2 && marks_[marks_.size() - 2].tool == *tool_) {
            marks_.pop_back();
            if (current_ == marks_.size() - 1) {
                current_.reset();
                measureResult_->clear();
            }
        }
        if (!marks_.back().points.empty()) marks_.back().points.pop_back();
    } else {
        return;
    }
    refreshTool();
    redrawMarks();
}

void VueWindow::showResult(const Mark& mark) {
    const auto at = [&](std::size_t i) { return cv::Point2d(mark.points[i].x(), mark.points[i].y()); };
    try {
        if (mark.tool == Tool::Length) {
            const Measurement length(at(0), at(1), vue_);
            measureResult_->setText(tr("Longueur : %1 (%2 px)")
                                        .arg(micronsLabel(length.label()))
                                        .arg(length.pixelDistance(), 0, 'f', 1));
        } else {
            const VickersResult hv =
                vickersHardness(Measurement(at(0), at(1), vue_), Measurement(at(2), at(3), vue_),
                                load_->currentData().toDouble());
            measureResult_->setText(tr("%1\nDiagonales : %2 µm et %3 µm (moyenne %4 µm)")
                                        .arg(QString::fromStdString(hv.label()))
                                        .arg(hv.diagonal1Microns, 0, 'f', 2)
                                        .arg(hv.diagonal2Microns, 0, 'f', 2)
                                        .arg(hv.meanDiagonalMicrons, 0, 'f', 2));
        }
    } catch (const std::exception& e) {
        // e.g. two points dragged onto each other: say so, keep the Mark.
        measureResult_->setText(tr("Mesure impossible : %1").arg(QString::fromStdString(e.what())));
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
    for (const Mark& mark : marks_) {
        const std::vector<QPointF>& p = mark.points;
        handles.insert(handles.end(), p.begin(), p.end());
        if (p.size() >= 2) view_->addLine(p[0], p[1]);
        if (p.size() == 4) view_->addLine(p[2], p[3]);
        if (p.size() != pointsNeeded(mark.tool)) continue;
        const auto at = [&](std::size_t i) { return cv::Point2d(p[i].x(), p[i].y()); };
        try {
            const Measurement first(at(0), at(1), vue_);
            view_->addLabel((p[0] + p[1]) / 2, micronsLabel(first.label()));
            if (mark.tool == Tool::Vickers) {
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
}

void VueWindow::analysePhases() {
    try {
        const WaitCursor wait;
        // On the photo as captured: Levels would shift the grey levels, and the
        // Échelle's bar and the phase map would be counted as phases.
        phases_ = zone_ ? analyzePhasesInZone(vue_.original(), *zone_) : analyzePhases(vue_.original());
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
    zoneLabel_->setText(tr("Zone analysée : %1x%2 px, %3 % de l'image.")
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
