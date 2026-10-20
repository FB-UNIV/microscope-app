// User journey:
//   As a teacher setting up a lab PC's camera from the app (issue #83), I
//   pick the camera the wizard found, try modes, exposures and colour
//   temperatures on the live preview, see what the camera actually granted,
//   and before saving I'm told which Calibrations stop applying (ADR 0015)
//   and whether the White reference must be measured again (ADR 0014).
//
// QtTest, run offscreen by CTest. The dialog never touches a camera: this
// test answers its requests the way MainWindow does with a real one.

#include "camera_setup_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest>

#include <memory>

using namespace msapp;

// Read back from the QSignalSpy of profileRequested / controlsRequested.
Q_DECLARE_METATYPE(msapp::CameraConfig)

namespace {

Calibration x5At4k() {
    Calibration c;
    c.grossissement = "x5";
    c.pixelsPerMicron = 1.55;
    c.frameWidth = 3840;
    c.frameHeight = 2160;
    return c;
}

WorkstationConfig labWorkstation() {
    WorkstationConfig w;
    w.camera = moticamA5Profile();
    w.camera.whiteReference = {1.745, 1.0, 2.576};
    w.setCalibration(x5At4k());
    return w;
}

// What the camera reports when it grants `profile` exactly.
ProfileReport granted(const CameraConfig& profile) {
    ProfileReport r;
    r.exposureApplied = true;
    r.whiteBalanceApplied = true;
    r.frameSize = cv::Size(profile.width, profile.height);
    r.pixelFormat = profile.pixelFormat;
    r.frameSizeApplied = true;
    r.pixelFormatApplied = true;
    return r;
}

std::vector<FoundCamera> labCameras() {
    FoundCamera dshow;
    dshow.deviceIndex = 0;
    dshow.captureApi = "dshow";
    dshow.defaultSize = cv::Size(1024, 768);
    dshow.deliversFrames = true;
    FoundCamera msmf = dshow;
    msmf.captureApi = "msmf";
    return {dshow, msmf};
}

template <typename T>
T* child(QWidget* parent, const char* name) {
    T* widget = parent->findChild<T*>(QString::fromLatin1(name));
    if (widget == nullptr) qFatal("no child widget named %s", name);
    return widget;
}

int modeIndex(QComboBox* modes, int width, int height) {
    for (int i = 0; i < modes->count(); ++i) {
        if (modes->itemData(i).toSize() == QSize(width, height)) return i;
    }
    return -1;
}

CameraConfig takeProfile(QSignalSpy& spy) {
    return spy.takeFirst().at(0).value<CameraConfig>();
}

} // namespace

class TestCameraSetupDialog : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<CameraSetupDialog> dialog_;
    std::unique_ptr<QSignalSpy> search_;
    std::unique_ptr<QSignalSpy> profile_;
    std::unique_ptr<QSignalSpy> controls_;
    std::unique_ptr<QSignalSpy> white_;

    void create(const WorkstationConfig& saved, const CameraConfig& start) {
        dialog_ = std::make_unique<CameraSetupDialog>(saved, start, std::vector<std::string>{"dshow", "msmf"});
        search_ = std::make_unique<QSignalSpy>(dialog_.get(), &CameraSetupDialog::searchRequested);
        profile_ = std::make_unique<QSignalSpy>(dialog_.get(), &CameraSetupDialog::profileRequested);
        controls_ = std::make_unique<QSignalSpy>(dialog_.get(), &CameraSetupDialog::controlsRequested);
        white_ = std::make_unique<QSignalSpy>(dialog_.get(), &CameraSetupDialog::whiteReferenceRequested);
    }

    // Opens the dialog on the lab workstation, answers its first search, and
    // reports the camera open on the A5 profile.
    void openOnLabWorkstation() {
        const WorkstationConfig saved = labWorkstation();
        create(saved, saved.camera);
        dialog_->show();
        QTRY_COMPARE(search_->count(), 1);
        dialog_->camerasFound(labCameras(), QString());
        dialog_->cameraOpened(granted(saved.camera));
    }

    void goToSavePage() {
        auto* next = child<QPushButton>(dialog_.get(), "nextButton");
        for (int page = 0; page < 10 && next->isVisible(); ++page) next->click();
    }

private slots:
    void initTestCase() { qRegisterMetaType<msapp::CameraConfig>(); }

    void cleanup() {
        search_.reset();
        profile_.reset();
        controls_.reset();
        white_.reset();
        dialog_.reset();
    }

    void listsTheCamerasFoundAndSelectsTheOneInUse() {
        openOnLabWorkstation();
        auto* list = child<QListWidget>(dialog_.get(), "cameraList");
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("DirectShow")));
        QVERIFY(list->item(0)->text().contains(QStringLiteral("1024×768")));
        QCOMPARE(list->currentRow(), 0);  // the A5 profile's camera: index 0, DirectShow
        QCOMPARE(profile_->count(), 0);   // showing it selected reopens nothing

        list->setCurrentRow(1);
        QCOMPARE(profile_->count(), 1);
        const CameraConfig tried = takeProfile(*profile_);
        QCOMPARE(tried.captureApi, std::string("msmf"));
        QCOMPARE(tried.deviceIndex, 0);
    }

    void savingTheSameProfileChangesNothing() {
        openOnLabWorkstation();
        goToSavePage();
        auto* save = child<QPushButton>(dialog_.get(), "saveButton");
        QVERIFY(save->isVisible());
        QVERIFY(save->isEnabled());
        const CameraSetupReview review = dialog_->review();
        QVERIFY(!review.changed);
        QVERIFY(!review.calibrationsLost());
        QVERIFY(!review.whiteReferenceStale);
        QVERIFY(!dialog_->recalibrateRequested());
    }

    void aNewModeReopensTheCameraAndWarnsAboutCalibrations() {
        openOnLabWorkstation();
        auto* modes = child<QComboBox>(dialog_.get(), "cameraMode");
        const int hd = modeIndex(modes, 1920, 1080);
        QVERIFY(hd >= 0);
        modes->setCurrentIndex(hd);
        QCOMPARE(profile_->count(), 1);
        const CameraConfig tried = takeProfile(*profile_);
        QCOMPARE(tried.width, 1920);
        QCOMPARE(tried.height, 1080);

        goToSavePage();
        auto* save = child<QPushButton>(dialog_.get(), "saveButton");
        QVERIFY(!save->isEnabled());  // nothing saved before the camera says what it granted

        dialog_->cameraOpened(granted(tried));
        QVERIFY(save->isEnabled());
        const CameraSetupReview review = dialog_->review();
        QVERIFY(review.changed);
        QCOMPARE(review.unusableCalibrations.size(), std::size_t(1));
        QVERIFY(child<QCheckBox>(dialog_.get(), "recalibrate")->isVisible());
        QVERIFY(dialog_->recalibrateRequested());
        QVERIFY(child<QLabel>(dialog_.get(), "summary")->text().contains(QStringLiteral("x5")));
    }

    void aModeTheCameraFallsBackFromIsSavedAsGranted() {
        openOnLabWorkstation();
        ProfileReport fallback = granted(moticamA5Profile());
        fallback.frameSize = cv::Size(1024, 768);
        fallback.frameSizeApplied = false;
        dialog_->cameraOpened(fallback);
        QVERIFY(child<QLabel>(dialog_.get(), "grantedMode")->text().contains(QStringLiteral("1024×768")));
        const CameraSetupReview review = dialog_->review();
        QVERIFY(review.frameSizeAdopted);
        QCOMPARE(review.profile.width, 1024);
        QCOMPARE(review.profile.height, 768);
        goToSavePage();
        QVERIFY(child<QLabel>(dialog_.get(), "summary")->text().contains(QStringLiteral("fourni par la caméra")));
    }

    void exposureAndColourAreTriedLiveAndStaleTheWhiteReference() {
        openOnLabWorkstation();
        auto* exposure = child<QComboBox>(dialog_.get(), "exposure");
        QVERIFY(child<QRadioButton>(dialog_.get(), "fixedExposure")->isChecked());
        QCOMPARE(exposure->currentData().toDouble(), 7.8125);
        exposure->setCurrentIndex(exposure->findData(3.90625));
        QCOMPARE(controls_->count(), 1);
        QCOMPARE(profile_->count(), 0);  // no reopen for a control
        QCOMPARE(takeProfile(*controls_).exposureMs, std::optional<double>(3.90625));
        QVERIFY(dialog_->review().whiteReferenceStale);

        // Measured again under the new exposure: it holds.
        child<QPushButton>(dialog_.get(), "measureWhite")->click();
        QCOMPARE(white_->count(), 1);
        dialog_->whiteReferenceMeasured({1.6, 1.0, 2.4});
        QVERIFY(!dialog_->review().whiteReferenceStale);
        QCOMPARE(dialog_->proposed().whiteReference.red, 1.6);

        // A new colour temperature drops it again, and the preview reopens
        // without its gains.
        child<QSpinBox>(dialog_.get(), "kelvin")->setValue(4500);
        QCOMPARE(profile_->count(), 1);
        const CameraConfig tried = takeProfile(*profile_);
        QCOMPARE(tried.whiteBalanceKelvin, std::optional<double>(4500.0));
        QCOMPARE(tried.whiteReference.red, 1.745);
        QVERIFY(dialog_->review().whiteReferenceStale);
    }

    void restoringTheA5ProfileKeepsTheWhiteReference() {
        const WorkstationConfig saved = labWorkstation();
        CameraConfig start = saved.camera;
        start.width = 1280;
        start.height = 720;
        start.exposureMs.reset();
        start.whiteBalanceKelvin.reset();
        create(saved, start);
        QVERIFY(!child<QRadioButton>(dialog_.get(), "fixedExposure")->isChecked());
        child<QPushButton>(dialog_.get(), "restoreA5")->click();
        QCOMPARE(profile_->count(), 1);
        const CameraConfig a5 = moticamA5Profile();
        const CameraConfig& proposed = dialog_->proposed();
        QCOMPARE(proposed.width, a5.width);
        QCOMPARE(proposed.pixelFormat, a5.pixelFormat);
        QCOMPARE(proposed.exposureMs, a5.exposureMs);
        QCOMPARE(proposed.whiteBalanceKelvin, a5.whiteBalanceKelvin);
        QCOMPARE(proposed.whiteReference.blue, 2.576);
        QVERIFY(child<QRadioButton>(dialog_.get(), "fixedExposure")->isChecked());
        QVERIFY(child<QRadioButton>(dialog_.get(), "lockedColour")->isChecked());
    }

    void aCameraThatFailsCannotBeSaved() {
        openOnLabWorkstation();
        dialog_->cameraFailed(QStringLiteral("no camera at index 0"));
        goToSavePage();
        QVERIFY(!child<QPushButton>(dialog_.get(), "saveButton")->isEnabled());
        QVERIFY(child<QLabel>(dialog_.get(), "summary")->text().contains(QStringLiteral("no camera at index 0")));
    }
};

QTEST_MAIN(TestCameraSetupDialog)
#include "test_camera_setup_dialog.moc"
