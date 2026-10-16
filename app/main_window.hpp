#pragma once

#include "catalog.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QStringList>

#include <filesystem>
#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QThread;

namespace msapp {

class CameraJobs;
class LocationPicker;
class MireDialog;
class PreviewWidget;

// Everything main() loaded for the window.
struct AppSetup {
    WorkstationConfig config;             // saved back when the Poste menu changes it
    std::filesystem::path configPath;     // empty: those changes can't be saved
    CameraConfig camera;                  // config.camera plus this run's command-line overrides
    Catalog catalog;
    std::filesystem::path catalogPath;    // empty: no catalog file to open or reload
    std::filesystem::path vueRoot;        // where VueStore files the Vues
    QStringList warnings;                 // for the banner, e.g. a file that couldn't be read
};

// The capture app's main window (issues #19–#22). A student picks the
// Department, TP and Échantillon, sees the live preview, picks the
// Grossissement and captures: VueStore files the Vue (#18) with that
// Grossissement's Calibration when it holds for the camera's frame size
// (ADR 0015) — otherwise uncalibrated, never refused, and the banner says
// why. The side panel shows the TP's instructions and the Échantillon's
// Vues, one of them flagged Vue principale (ADR 0016), or the Unsorted
// Vues, each filed into its Échantillon with Classer la Vue (ADR 0008) —
// as is a Vue captured under the wrong Échantillon. F9 hides the panel for
// a bigger live view (remembered in ui.ini beside workstation.json). The
// Poste menu measures the White reference (ADR 0014) and calibrates a
// Grossissement on the mire.
//
// The camera lives on a worker thread (PreviewWorker); a capture, a White
// reference or a still for the mire is a CameraJob run there between two
// frames.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(AppSetup setup, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void buildMenus();
    // (Re)opens the camera on a fresh worker thread.
    void startCamera();
    // Asks the worker to stop and waits for it to close the camera.
    void stopCamera();

    void refreshBanner();
    // The Grossissement's Calibration against the camera's frame size, and
    // whether a capture can start.
    void refreshCaptureState();
    // The TP's instructions and the listed Vues.
    void refreshVues();
    void loadNextThumbnail();

    void capture();
    void captured(int generation, const std::filesystem::path& vue, const QString& error);
    void flagPrincipale();
    void openVue();
    void fileSelectedVue();
    void generateFiche();
    void measureWhiteReference();
    void calibrateGrossissement();
    void requestStill(QPointer<MireDialog> dialog);
    void openCatalogFile();
    void reloadCatalog();
    void openVueFolder();
    bool saveConfig();

    std::optional<VueLocation> location() const;
    // Whose Vues the side panel lists: the picked location, or Unsorted
    // while Vues non classées is on.
    std::optional<VueLocation> listedLocation() const;
    QString grossissement() const;
    void fillGrossissements();

    AppSetup setup_;
    VueStore store_;
    QString catalogWarning_;
    QStringList cameraWarnings_;
    QString captureWarning_;

    PreviewWidget* preview_ = nullptr;
    QLabel* banner_ = nullptr;
    LocationPicker* picker_ = nullptr;
    QComboBox* grossissement_ = nullptr;
    QLabel* calibrationLabel_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QLabel* instructions_ = nullptr;
    // With a catalog the picker offers only Échantillons: this lists the
    // Unsorted Vues instead, while there are any.
    QPushButton* unsortedButton_ = nullptr;
    QLabel* vuesTitle_ = nullptr;
    QListWidget* vueList_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;

    QThread* thread_ = nullptr;
    std::shared_ptr<CameraJobs> jobs_;
    // Bumped whenever the camera restarts or stops: signals and job results
    // a previous worker had already queued are ignored instead of shown.
    int generation_ = 0;
    bool cameraOpen_ = false;
    bool busy_ = false;     // a capture is running on the camera thread
    QSize frameSize_;       // the latest frame as grabbed, full size
};

} // namespace msapp
