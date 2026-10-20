#pragma once

#include "measurement_marks.hpp"
#include "phase_analysis.hpp"
#include "vue.hpp"
#include "vue_store.hpp"

#include <QImage>
#include <QMainWindow>
#include <QPointF>
#include <QRectF>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolButton;
class QWidget;

namespace msapp {

class HistogramWidget;
class ImageView;
class LevelsWidget;

// One Vue, opened from its Échantillon's list (issues #23–#25), laid out by
// task (#89): the tools that act on the photo in a toolbar above it — undo
// and redo, Longueur, Vickers, Zone, zoom — and a panel with one tab per
// task:
// - Image: Levels on the photo's histogram, with Auto and a visible
//   pending state (#93), the Échelle, the Treatments (ADR 0002). Each
//   change is saved at once in the sidecar, never into the stored photo
//   (ADR 0003), and can be undone; a sidecar that lost something says so;
// - Mesures: Measurement and Dureté Vickers by clicking on the photo, with
//   its own Calibration (ADR 0015) — refused on an uncalibrated Vue. While a
//   tool waits for a point, a guide shows the length to the cursor, a loupe
//   the pixels under it, and a prompt over the photo what to click (#90);
//   every point can be dragged afterwards, and Échap puts the tool away;
// - Phases: CALCUL, the percentages, the histogram with the Seuil — on the
//   whole photo or on a zone (#55), kept with the phase map in the sidecar.
// Above the tabs, the Grossissement and flagging it as its Échantillon's
// Vue principale (ADR 0016); under them the capture details, folded, and
// asking the main window to delete it (to the Corbeille).
class VueWindow : public QMainWindow {
    Q_OBJECT

public:
    VueWindow(Vue vue, VueStore store, VueLocation location, QWidget* parent = nullptr);

    const std::filesystem::path& vuePath() const { return vue_.imagePath(); }
    // Drops Levels previewed but not applied, so closing doesn't ask.
    void discardPendingChanges();

signals:
    void principaleChanged();
    // "Supprimer la Vue…" was clicked; the main window confirms and deletes.
    void deleteRequested();

protected:
    // Levels previewed but not applied: asks whether to apply them first.
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Tool { Length, Vickers, Zone };

    QWidget* buildImagePage();
    QWidget* buildMeasuresPage();
    QWidget* buildPhasesPage();

    // The Vue as rendered with `levels` in place of its own Levels (a live
    // preview), or exactly as saved.
    QImage render(std::optional<std::pair<int, int>> levels = std::nullopt) const;
    void showSaved();
    void refreshTreatments();
    void refreshPrincipale();
    std::optional<std::size_t> indexOf(const std::string& type) const;
    // Runs a pipeline edit, telling the user when the sidecar can't be
    // saved, and keeps what the pipeline was for undo.
    template <typename Edit>
    void edit(Edit change);
    // Puts back the pipeline before the last edit (undo) or after the last
    // undone one (redo).
    void stepHistory(bool back);
    // After the pipeline changed: the photo, the list, the Levels fields
    // when the saved Levels changed, undo and redo.
    void pipelineChanged(std::pair<int, int> levelsBefore);
    void refreshHistory();

    // The Levels saved with the Vue; 0–255 without any.
    std::pair<int, int> savedLevels() const;
    bool levelsPending() const;
    // The spin boxes and handles back to the saved Levels.
    void showSavedLevels();
    void setLevelsFields(int black, int white);
    // The pending state, the Apply / Ignore buttons and the handles.
    void refreshLevels();
    void previewLevels();
    void applyLevels();
    // Auto: the stretch autoLevels() proposes, previewed.
    void stretchLevels();
    void addEchelle();
    void removeTreatment();

    // Starts `tool`, or puts it away when it is the one already active.
    void startTool(Tool tool);
    // Puts the active tool away (Échap), dropping a measurement it left
    // half-done.
    void cancelTool();
    void clicked(QPointF point);
    void handleMoved(int index, QPointF point);
    // Right-click on a point: its finished measurement goes; a point of the
    // one being clicked goes alone.
    void removeHandle(int index);
    // Backspace: takes back the last point clicked, reopening the reading
    // just finished if need be.
    void undoPoint();
    // The zone tool's rectangle, dragged or clicked corner to corner.
    void applyZone(QRectF corners);
    // Draws the zone and every Mark, with its values, and makes the Marks'
    // points draggable.
    void redrawMarks();
    // The active tool's buttons checked, its prompt over the photo, the
    // loupe, and the tab that goes with it.
    void refreshTool();
    // The cursor over the photo: the guide from the last point, the
    // position in the status bar.
    void hovered(QPointF point);
    void refreshGuide();
    void showResult(const Mark& mark);
    void analysePhases();
    // The zone CALCUL and the phase map work on; empty = the whole photo.
    // Forgets results computed on the previous one.
    void setZone(std::optional<cv::Rect> zone);
    void refreshZone();
    void showPhaseMap();
    void exportCopy();
    void flagPrincipale();

    Vue vue_;
    VueStore store_;
    VueLocation location_;
    std::array<std::uint64_t, 256> levelsHistogram_{};

    ImageView* view_;
    QLabel* banner_;
    QTabWidget* tabs_;
    QWidget* imagePage_;
    QWidget* measuresPage_;
    QWidget* phasesPage_;
    QAction* undo_;
    QAction* redo_;
    QLabel* zoomLabel_;
    QLabel* positionLabel_;

    LevelsWidget* levels_;
    QSpinBox* black_;
    QSpinBox* white_;
    QLabel* levelsState_;
    QPushButton* applyLevels_;
    QPushButton* ignoreLevels_;
    QTimer* levelsTimer_;
    QListWidget* treatments_;
    QPushButton* echelle_;

    QToolButton* lengthTool_;
    QToolButton* vickersTool_;
    QToolButton* zoneTool_;
    QComboBox* load_;
    QLabel* measureResult_;

    QLabel* phaseResult_;
    QLabel* zoneLabel_;
    HistogramWidget* histogram_;
    QPushButton* phaseMap_;
    QPushButton* principale_;

    // Pipelines to come back to, as Vue::treatmentsJson() gives them.
    std::vector<nlohmann::json> undoStack_;
    std::vector<nlohmann::json> redoStack_;
    bool discardOnClose_ = false;

    std::optional<Tool> tool_;
    // Finished Marks, then — while a measuring tool is active — the one
    // being clicked; its current() is the Mark measureResult_ shows.
    MeasurementMarks marks_;
    std::vector<QPointF> clicks_;    // the zone's first corner, while picking it
    std::optional<QPointF> cursor_;  // over the photo, in image pixels
    std::optional<PhaseAnalysis> phases_;
    std::optional<cv::Rect> zone_;
};

} // namespace msapp
