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
//   Calibration (ADR 0015) — refused on an uncalibrated Vue;
// - Phase analysis: CALCUL, the percentages, the histogram with the Seuil;
// - flagging it as its Échantillon's Vue principale (ADR 0016).
class VueWindow : public QMainWindow {
    Q_OBJECT

public:
    VueWindow(Vue vue, VueStore store, VueLocation location, QWidget* parent = nullptr);

    const std::filesystem::path& vuePath() const { return vue_.imagePath(); }

signals:
    void principaleChanged();

private:
    enum class Tool { Length, Vickers };

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
    void startTool(Tool tool);
    void clicked(QPointF point);
    void analysePhases();
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
    QLabel* measureResult_;
    QLabel* phaseResult_;
    HistogramWidget* histogram_;
    QPushButton* phaseMap_;
    QPushButton* principale_;

    std::optional<Tool> tool_;
    std::vector<QPointF> clicks_;
    std::optional<PhaseAnalysis> phases_;
};

} // namespace msapp
