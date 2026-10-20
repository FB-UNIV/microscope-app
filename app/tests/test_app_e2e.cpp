// GUI end-to-end journey (the e2e pipeline's app stage), offscreen:
//   A student at a workstation whose x10 objective is calibrated sees the
//   live preview, captures the Échantillon picked in the toolbar, finds the
//   new Vue in the side panel, opens it, runs CALCUL and measures a length.
//
// The real MainWindow and VueWindow run on the replay camera (the app's
// --replay option): the camera thread, its queued signals, the CameraJobs
// a capture goes through, the VueStore files and the Vue's sidecar are all
// the app's own. Widgets are found by their object names. A modal message
// box would hang the event loop, so any that opens is closed and recorded
// as a failure.

#include "image_view.hpp"
#include "main_window.hpp"
#include "qt_image.hpp"
#include "vue_window.hpp"

#include "catalog.hpp"
#include "vue.hpp"
#include "vue_store.hpp"
#include "workstation_config.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace msapp;

namespace {

const cv::Size kFrame(768, 512);
constexpr double kX10PxPerMicron = 1.55;
const VueLocation kAc1{"GMP", "TP 1: Aciers", "Ac1: XC10"};  // the picker's first Échantillon

// A real micrograph: the in-repo legacy crop of a steel, tiled to the frame.
cv::Mat specimen() {
    const fs::path crop = fs::path(MSAPP_SOURCE_DIR) / "tests" / "fixtures" / "legacy" / "CTTM1Ech1Vue1_crop256.png";
    const cv::Mat tile = cv::imread(crop.string(), cv::IMREAD_COLOR);
    if (tile.empty()) throw std::runtime_error("missing fixture " + crop.string());
    cv::Mat image(kFrame, CV_8UC3);
    for (int y = 0; y < image.rows; y += tile.rows) {
        for (int x = 0; x < image.cols; x += tile.cols) {
            const cv::Rect cell(x, y, std::min(tile.cols, image.cols - x), std::min(tile.rows, image.rows - y));
            tile(cv::Rect(0, 0, cell.width, cell.height)).copyTo(image(cell));
        }
    }
    return image;
}

} // namespace

class AppE2E : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void previewRunsOnTheReplayCamera();
    void captureFilesACalibratedVue();
    void openTheVueMeasureAndAnalyse();
    void cameraSetupCancelledKeepsTheSavedProfile();
    void cleanupTestCase();

private:
    // What main() would load on this workstation: the replay camera, x10
    // calibrated at the replayed frame size, the example catalog.
    AppSetup workstation() const;

    QTemporaryDir dir_;
    fs::path root_;
    std::unique_ptr<MainWindow> window_;
    QTimer dialogWatch_;
    QStringList dialogs_;  // every message box the app opened
};

AppSetup AppE2E::workstation() const {
    AppSetup setup;
    setup.configPath = root_ / "config" / "workstation.json";
    WorkstationConfig config;
    config.camera.backend = "file";
    config.camera.frames = {root_ / "specimen.png"};
    config.camera.width = kFrame.width;
    config.camera.height = kFrame.height;
    config.camera.pixelFormat.clear();
    Calibration x10;
    x10.grossissement = "x10";
    x10.pixelsPerMicron = kX10PxPerMicron;
    x10.frameWidth = kFrame.width;
    x10.frameHeight = kFrame.height;
    config.setCalibration(x10);
    saveWorkstationConfig(config, setup.configPath);
    setup.config = config;
    setup.camera = config.camera;
    setup.catalog = loadCatalog(fs::path(MSAPP_SOURCE_DIR) / "docs" / "catalog-example.json").catalog;
    setup.vueRoot = root_ / "vues";
    return setup;
}

void AppE2E::initTestCase() {
    QVERIFY(dir_.isValid());
    root_ = toPath(dir_.path());
    QVERIFY(cv::imwrite((root_ / "specimen.png").string(), specimen()));

    connect(&dialogWatch_, &QTimer::timeout, this, [this] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            dialogs_ << box->windowTitle() + QStringLiteral(": ") + box->text();
            box->reject();
        }
    });
    dialogWatch_.start(100);

    window_ = std::make_unique<MainWindow>(workstation());
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_.get()));
}

void AppE2E::previewRunsOnTheReplayCamera() {
    auto* capture = window_->findChild<QPushButton*>(QStringLiteral("captureButton"));
    auto* fps = window_->findChild<QLabel*>(QStringLiteral("fpsLabel"));
    QVERIFY(capture != nullptr && fps != nullptr);
    // The camera thread opened the replay camera, and the picker starts on
    // the catalog's first Échantillon.
    QTRY_VERIFY_WITH_TIMEOUT(capture->isEnabled(), 15000);
    // Frames reach the window: the rate shows about once a second. Waiting
    // for it also means the frame size is known, so x10 can be checked
    // against it before the capture.
    QTRY_VERIFY_WITH_TIMEOUT(fps->text().contains(QStringLiteral("images/s")), 15000);
    QVERIFY2(dialogs_.isEmpty(), qPrintable(dialogs_.join(QLatin1Char('\n'))));
}

void AppE2E::captureFilesACalibratedVue() {
    auto* capture = window_->findChild<QPushButton*>(QStringLiteral("captureButton"));
    auto* list = window_->findChild<QListWidget*>(QStringLiteral("vueList"));
    QVERIFY(capture != nullptr && list != nullptr);
    QCOMPARE(list->count(), 0);

    QTest::mouseClick(capture, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(capture->isEnabled(), 15000);  // ready for the next one
    QVERIFY2(dialogs_.isEmpty(), qPrintable(dialogs_.join(QLatin1Char('\n'))));

    const auto vues = VueStore(root_ / "vues").listVues(kAc1);
    QCOMPARE(static_cast<int>(vues.size()), 1);
    const Vue vue = Vue::load(vues.front());
    QVERIFY(!vue.sidecarError());
    QCOMPARE(vue.original().cols, kFrame.width);
    QCOMPARE(vue.original().rows, kFrame.height);
    QVERIFY(vue.captureInfo().has_value());
    QCOMPARE(QString::fromStdString(vue.captureInfo()->cameraBackend), QStringLiteral("file"));
    QVERIFY2(vue.calibration().has_value(), "the x10 Calibration holds for the replayed frame size");
    QCOMPARE(QString::fromStdString(vue.calibration()->grossissement), QStringLiteral("x10"));
    QCOMPARE(static_cast<int>(vue.treatmentCount()), 1);
    QCOMPARE(QString::fromStdString(vue.treatment(0).type()), QStringLiteral("echelle"));
}

void AppE2E::openTheVueMeasureAndAnalyse() {
    auto* list = window_->findChild<QListWidget*>(QStringLiteral("vueList"));
    auto* open = window_->findChild<QPushButton*>(QStringLiteral("openVueButton"));
    QVERIFY(list != nullptr && open != nullptr);
    list->setCurrentRow(0);
    QTest::mouseClick(open, Qt::LeftButton);
    VueWindow* vueWindow = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((vueWindow = window_->findChild<VueWindow*>()) != nullptr, 5000);
    QVERIFY(QTest::qWaitForWindowExposed(vueWindow));

    // CALCUL: the phase percentages of the whole photo.
    auto* calcul = vueWindow->findChild<QPushButton*>(QStringLiteral("calculButton"));
    auto* phaseResult = vueWindow->findChild<QLabel*>(QStringLiteral("phaseResult"));
    QVERIFY(calcul != nullptr && phaseResult != nullptr);
    QTest::mouseClick(calcul, Qt::LeftButton);
    QVERIFY2(phaseResult->text().contains(QStringLiteral("Phase sombre")), qPrintable(phaseResult->text()));

    // A length of 100 µm at x10, clicked on the image.
    auto* lengthTool = vueWindow->findChild<QPushButton*>(QStringLiteral("lengthTool"));
    auto* view = vueWindow->findChild<ImageView*>(QStringLiteral("imageView"));
    auto* result = vueWindow->findChild<QLabel*>(QStringLiteral("measureResult"));
    QVERIFY(lengthTool != nullptr && view != nullptr && result != nullptr);
    QVERIFY2(lengthTool->isEnabled(), "a calibrated Vue can be measured");
    QTest::mouseClick(lengthTool, Qt::LeftButton);
    const QPoint from = view->mapFromScene(QPointF(100.0, 200.0));
    const QPoint to = view->mapFromScene(QPointF(100.0 + 100.0 * kX10PxPerMicron, 200.0));
    QVERIFY(view->viewport()->rect().contains(from) && view->viewport()->rect().contains(to));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    const QRegularExpressionMatch length =
        QRegularExpression(QStringLiteral("Longueur : ([0-9.]+) µm")).match(result->text());
    QVERIFY2(length.hasMatch(), qPrintable(result->text()));
    // The clicks land on whole screen pixels: within a pixel of 155 px.
    QVERIFY2(qAbs(length.captured(1).toDouble() - 100.0) < 1.0, qPrintable(result->text()));

    vueWindow->close();
    QVERIFY2(dialogs_.isEmpty(), qPrintable(dialogs_.join(QLatin1Char('\n'))));
}

// Poste › Configurer la caméra (#83) on a PC with no camera: the wizard
// searches (the replay camera is closed meanwhile), tries the A5 profile,
// which fails here, and Annuler puts the replay camera back, the saved
// profile untouched.
void AppE2E::cameraSetupCancelledKeepsTheSavedProfile() {
    QAction* configure = nullptr;
    for (QAction* action : window_->findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Configurer la caméra…")) configure = action;
    }
    QVERIFY(configure != nullptr);
    // The wizard runs modally, in an event loop of its own that this test
    // can't step past: a timer inside that loop waits for the search to end,
    // then cancels, as dialogWatch_ does with a message box.
    bool searched = false;
    bool saveEnabled = true;
    QTimer drive;
    connect(&drive, &QTimer::timeout, this, [&] {
        auto* wizard = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (wizard == nullptr || wizard->objectName() != QStringLiteral("cameraSetupDialog")) return;
        auto* status = wizard->findChild<QLabel*>(QStringLiteral("searchStatus"));
        if (status == nullptr || status->text().startsWith(QStringLiteral("Recherche"))) return;
        auto* save = wizard->findChild<QPushButton*>(QStringLiteral("saveButton"));
        saveEnabled = save != nullptr && save->isEnabled();
        searched = true;
        drive.stop();
        wizard->reject();
    });
    drive.start(100);
    QTimer::singleShot(0, configure, &QAction::trigger);
    QTRY_VERIFY_WITH_TIMEOUT(searched, 60000);
    QVERIFY2(!saveEnabled, "nothing to save while the camera doesn't open");

    auto* capture = window_->findChild<QPushButton*>(QStringLiteral("captureButton"));
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::activeModalWidget() == nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(capture->isEnabled(), 15000);  // the replay camera runs again
    const LoadedWorkstationConfig saved = loadWorkstationConfig(root_ / "config" / "workstation.json");
    QVERIFY(saved.error.empty());
    QCOMPARE(QString::fromStdString(saved.config.camera.backend), QStringLiteral("file"));
    QVERIFY2(dialogs_.isEmpty(), qPrintable(dialogs_.join(QLatin1Char('\n'))));
}

void AppE2E::cleanupTestCase() {
    // Stops the camera thread and waits for it: a hang here fails on the
    // CTest timeout.
    window_.reset();
    dialogWatch_.stop();
}

QTEST_MAIN(AppE2E)
#include "test_app_e2e.moc"
