#pragma once

#include "capture_station.hpp"
#include "catalog.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QThreadPool>

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
class CameraSetupDialog;
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
// as is a Vue captured under the wrong Échantillon — or deleted, to the
// Corbeille of the Vue folder, where it can still be recovered. F9 hides
// the panel for a bigger live view (remembered in ui.ini beside
// workstation.json). The Poste menu sets up the camera (#83), measures the
// White reference (ADR 0014) and calibrates a Grossissement on the mire.
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
    // (Re)opens the camera on a fresh worker thread, with the profile the
    // camera setup wizard is trying, if open, else setup_.camera.
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
    // A thumbnail decoded on thumbnailPool_; null when the file can't be read.
    void thumbnailLoaded(const QString& path, const QImage& thumbnail);

    void capture();
    void captured(int generation, const std::filesystem::path& vue, const QString& error);
    void flagPrincipale();
    void openVue();
    void fileSelectedVue();
    void deleteSelectedVue();
    // Asks, closes any window open on `vue`, then moves it to the Corbeille
    // (VueStore::deleteVue). From the list or from the Vue's own window.
    void deleteVue(const std::filesystem::path& vue);
    void generateFiche();
    void measureWhiteReference();
    void calibrateGrossissement();
    // Poste › Configurer la caméra (#83), and its requests: the wizard
    // tries each profile on this window's camera thread.
    void configureCamera();
    void searchCameras();
    void tryCameraControls(const CameraConfig& profile);
    void measureSetupWhiteReference();
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
    ProfileReport report_;  // what the camera granted at its last open

    // The camera setup wizard while it is open: it gets the frames, and the
    // camera runs the profile it is trying.
    QPointer<CameraSetupDialog> setupDialog_;
    std::optional<CameraConfig> trialCamera_;
    // findCameras() runs here with the camera closed; startCamera() waits.
    QThreadPool cameraSearchPool_;
    bool searching_ = false;
    // Saved a camera mode the Calibrations don't fit: calibrate once open.
    bool calibrateWhenOpen_ = false;

    // The Vue list's thumbnails by image path, kept across refreshes: a Vue
    // never changes under its path (ADR 0004), and reading a 4K TIFF takes
    // about 125 ms, so each is decoded once — one at a time, off the GUI
    // thread, on thumbnailPool_.
    QHash<QString, QIcon> thumbnails_;
    QThreadPool thumbnailPool_;
    bool thumbnailLoading_ = false;
};

} // namespace msapp
