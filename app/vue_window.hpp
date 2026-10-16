#pragma once

#include "phase_analysis.hpp"
#include "vue.hpp"
#include "vue_store.hpp"

#include <QImage>
#include <QMainWindow>
#include <QPointF>

#include <optional>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;

namespace msapp {

class HistogramWidget;
class ImageView;

// One Vue, opened from its Échantillon's list (issues #23–#25):
// - its Treatments (ADR 0002): Levels with a live preview, the Échelle,
//   the phase map; each change saved at once in the sidecar, never into the
//   stored photo (ADR 0003); a sidecar that lost something says so;
// - Measurement and Dureté Vickers by clicking on the photo, with its own
//   Calibration (ADR 0015) — refused on an uncalibrated Vue; every point
//   can be dragged afterwards to correct it, the active tool is shown with
//   what to click next, and Échap puts it away (#56);
// - Phase analysis: CALCUL, the percentages, the histogram with the Seuil —
//   on the whole photo or on a zone picked with two clicks (#55), kept with
//   the phase map in the sidecar;
// - flagging it as its Échantillon's Vue principale (ADR 0016).
class VueWindow : public QMainWindow {
    Q_OBJECT

public:
    VueWindow(Vue vue, VueStore store, VueLocation location, QWidget* parent = nullptr);

    const std::filesystem::path& vuePath() const { return vue_.imagePath(); }

signals:
    void principaleChanged();

private:
    enum class Tool { Length, Vickers, Zone };

    // One measurement on the photo: two points for a length, four for a
    // Vickers indent (two diagonals). Kept, so its points can be dragged and
    // its value recomputed (#56); gone when the window closes, like every
    // Measurement.
    struct Mark {
        Tool tool;
        std::vector<QPointF> points;
    };
    static std::size_t pointsNeeded(Tool tool) { return tool == Tool::Vickers ? 4 : 2; }

    // The Vue as rendered with `levels` in place of its own Levels (a live
    // preview), or exactly as saved.
    QImage render(std::optional<std::pair<int, int>> levels = std::nullopt) const;
    void showSaved();
    void refreshTreatments();
    void refreshPrincipale();
    std::optional<std::size_t> indexOf(const std::string& type) const;
    // Runs a pipeline edit, telling the user when the sidecar can't be saved.
    template <typename Edit>
    void edit(Edit change);

    void previewLevels();
    void applyLevels();
    void addEchelle();
    void removeTreatment();
    // Starts `tool`, or puts it away when it is the one already active.
    void startTool(Tool tool);
    // Puts the active tool away (Échap), dropping a measurement it left
    // half-done.
    void cancelTool();
    void clicked(QPointF point);
    void handleMoved(int index, QPointF point);
    // Draws the zone and every Mark, with its values, and makes the Marks'
    // points draggable.
    void redrawMarks();
    // The active tool's buttons checked, and what to click next.
    void refreshTool();
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

    ImageView* view_;
    QLabel* banner_;
    QListWidget* treatments_;
    QSpinBox* black_;
    QSpinBox* white_;
    QTimer* levelsTimer_;
    QPushButton* echelle_;
    QPushButton* lengthTool_;
    QPushButton* vickersTool_;
    QComboBox* load_;
    QLabel* toolPrompt_;
    QLabel* measureResult_;
    QLabel* phaseResult_;
    QLabel* zoneLabel_;
    QPushButton* zoneTool_;
    HistogramWidget* histogram_;
    QPushButton* phaseMap_;
    QPushButton* principale_;

    std::optional<Tool> tool_;
    // Finished Marks, then — while a measuring tool is active — the one
    // being clicked.
    std::vector<Mark> marks_;
    // The Mark whose value measureResult_ shows.
    std::optional<std::size_t> current_;
    std::vector<QPointF> clicks_;  // the zone's first corner, while picking it
    std::optional<PhaseAnalysis> phases_;
    std::optional<cv::Rect> zone_;
};

} // namespace msapp
