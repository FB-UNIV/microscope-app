// User journey:
//   As a student in a Vue's window, I stretch the Levels with Auto, drag
//   the black handle a bit further, apply them, and undo when I change my
//   mind; closing with Levels not applied asks first. Measuring a length,
//   a dashed line from my first point follows the cursor with the length it
//   would read, and the Mesures tab comes up (#89, #90, #93).
//
// QtTest, run offscreen by CTest (QT_QPA_PLATFORM=offscreen), on a Vue
// written to a temporary folder: every change is checked in its sidecar.

#include "image_view.hpp"
#include "levels_treatment.hpp"
#include "levels_widget.hpp"
#include "qt_image.hpp"
#include "vue.hpp"
#include "vue_store.hpp"
#include "vue_window.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace msapp;

namespace {

// Grey levels 40 to 200, one per column: Auto has a known answer.
cv::Mat ramp() {
    cv::Mat image(120, 161, CV_8UC3);
    for (int x = 0; x < image.cols; ++x) image.col(x).setTo(cv::Scalar::all(40 + x));
    return image;
}

void moveTo(QWidget* widget, QPoint at, Qt::MouseButtons held) {
    QMouseEvent event(QEvent::MouseMove, QPointF(at), QPointF(widget->mapToGlobal(at)), Qt::NoButton, held,
                      Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

} // namespace

class TestVueWindow : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void autoStretchesAndApplySavesTheLevels();
    void draggingTheBlackHandleSetsTheField();
    void undoAndRedoPutThePipelineBack();
    void closingWithLevelsNotAppliedAsks();
    void measuringShowsTheLengthToTheCursor();

private:
    template <typename T>
    T* child(const char* name) const {
        T* widget = window_->findChild<T*>(QString::fromLatin1(name));
        if (widget == nullptr) qFatal("no child %s", name);
        return widget;
    }
    // The Levels in the sidecar, as a fresh load reads them; empty without.
    std::optional<std::pair<int, int>> savedLevels() const;

    QTemporaryDir dir_;
    int count_ = 0;
    std::filesystem::path photo_;
    QPointer<VueWindow> window_;
    // A message box that opens gets the button labelled answer_ clicked,
    // or is rejected.
    QTimer boxWatch_;
    QString answer_;
    int asked_ = 0;
};

std::optional<std::pair<int, int>> TestVueWindow::savedLevels() const {
    const Vue vue = Vue::load(photo_);
    for (std::size_t i = 0; i < vue.treatmentCount(); ++i) {
        if (const auto* levels = dynamic_cast<const LevelsTreatment*>(&vue.treatment(i))) {
            return std::make_pair(levels->blackPoint(), levels->whitePoint());
        }
    }
    return std::nullopt;
}

void TestVueWindow::init() {
    QVERIFY(dir_.isValid());
    const std::filesystem::path root = toPath(dir_.path());
    photo_ = root / ("vue" + std::to_string(++count_) + ".png");
    Vue::capture(ramp(), photo_, std::nullopt, Calibration{"x10", 2.0, 161, 120});
    window_ = new VueWindow(Vue::load(photo_), VueStore(root / "vues"), VueLocation{"GMP", "TP 1", "Ac1"});
    window_->show();
    QVERIFY(QTest::qWaitForWindowExposed(window_));

    answer_.clear();
    asked_ = 0;
    boxWatch_.disconnect();
    connect(&boxWatch_, &QTimer::timeout, this, [this] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr) return;
        ++asked_;
        for (QAbstractButton* button : box->buttons()) {
            if (button->text() == answer_) {
                button->click();
                return;
            }
        }
        box->reject();
    });
    boxWatch_.start(50);
}

void TestVueWindow::cleanup() {
    boxWatch_.stop();
    if (window_) {
        window_->discardPendingChanges();
        delete window_;
    }
}

void TestVueWindow::autoStretchesAndApplySavesTheLevels() {
    auto* apply = child<QPushButton>("applyLevels");
    QVERIFY(!apply->isEnabled());  // nothing pending yet

    QTest::mouseClick(child<QPushButton>("autoLevels"), Qt::LeftButton);
    QCOMPARE(child<QSpinBox>("levelsBlack")->value(), 40);
    QCOMPARE(child<QSpinBox>("levelsWhite")->value(), 200);
    QCOMPARE(child<LevelsWidget>("levelsWidget")->black(), 40);
    QVERIFY2(apply->isEnabled(), "an Auto stretch is a preview until applied");
    QVERIFY(!savedLevels());

    QTest::mouseClick(apply, Qt::LeftButton);
    QCOMPARE(savedLevels(), std::make_optional(std::make_pair(40, 200)));
    QVERIFY(!apply->isEnabled());
}

void TestVueWindow::draggingTheBlackHandleSetsTheField() {
    auto* levels = child<LevelsWidget>("levelsWidget");
    const int y = levels->height() - 6;  // the handles' strip
    const QPoint from(qRound(levels->xOf(0)), y);
    const QPoint to(qRound(levels->xOf(60)), y);
    QTest::mousePress(levels, Qt::LeftButton, Qt::NoModifier, from);
    moveTo(levels, to, Qt::LeftButton);
    QTest::mouseRelease(levels, Qt::LeftButton, Qt::NoModifier, to);
    QVERIFY2(std::abs(child<QSpinBox>("levelsBlack")->value() - 60) <= 1,
             qPrintable(QString::number(child<QSpinBox>("levelsBlack")->value())));
    QCOMPARE(child<QSpinBox>("levelsWhite")->value(), 255);
    QVERIFY(child<QPushButton>("applyLevels")->isEnabled());
}

void TestVueWindow::undoAndRedoPutThePipelineBack() {
    auto* undo = child<QAction>("undoAction");
    auto* redo = child<QAction>("redoAction");
    QVERIFY(!undo->isEnabled() && !redo->isEnabled());
    QTest::mouseClick(child<QPushButton>("autoLevels"), Qt::LeftButton);
    QTest::mouseClick(child<QPushButton>("applyLevels"), Qt::LeftButton);
    QVERIFY(savedLevels());
    QVERIFY(undo->isEnabled());

    undo->trigger();
    QVERIFY2(!savedLevels(), "undo takes the Levels out of the sidecar");
    QCOMPARE(child<QSpinBox>("levelsBlack")->value(), 0);
    QCOMPARE(child<QSpinBox>("levelsWhite")->value(), 255);
    QVERIFY(!child<QPushButton>("applyLevels")->isEnabled());
    QVERIFY(redo->isEnabled() && !undo->isEnabled());

    redo->trigger();
    QCOMPARE(savedLevels(), std::make_optional(std::make_pair(40, 200)));
    QCOMPARE(child<QSpinBox>("levelsBlack")->value(), 40);
}

void TestVueWindow::closingWithLevelsNotAppliedAsks() {
    child<QSpinBox>("levelsBlack")->setValue(30);

    answer_ = QStringLiteral("-");  // no such button: the box is rejected, like Annuler
    window_->close();
    QCOMPARE(asked_, 1);
    QVERIFY2(window_ && window_->isVisible(), "cancelled: the window stays");

    answer_ = QStringLiteral("Appliquer");
    window_->close();
    QCOMPARE(asked_, 2);
    QTRY_VERIFY(window_.isNull());
    QCOMPARE(savedLevels(), std::make_optional(std::make_pair(30, 255)));
}

void TestVueWindow::measuringShowsTheLengthToTheCursor() {
    QTest::mouseClick(child<QAbstractButton>("lengthTool"), Qt::LeftButton);
    QCOMPARE(child<QTabWidget>("vueTabs")->currentWidget()->objectName(), QStringLiteral("measuresPage"));
    auto* view = child<ImageView>("imageView");
    QVERIFY(view->prompt().contains(QStringLiteral("1/2")));

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(QPointF(20, 20)));
    QVERIFY(view->prompt().contains(QStringLiteral("2/2")));
    moveTo(view->viewport(), view->mapFromScene(QPointF(80, 20)), Qt::NoButton);
    // 60 px at 2 px/µm: 30 µm, to within a screen pixel (the mouse lands on
    // whole screen pixels, so 29.9 is as right as 30.1).
    const QString label = view->guideLabel();
    bool number = false;
    const double microns = label.section(QLatin1Char(' '), 0, 0).toDouble(&number);
    const double tolerance = 1.0 / view->zoom() / 2.0 + 0.01;
    QVERIFY2(number && label.endsWith(QStringLiteral(" µm")) && std::abs(microns - 30.0) <= tolerance,
             qPrintable(label));

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(QPointF(80, 20)));
    QVERIFY2(view->guideLabel().isEmpty(), "measured: no guide until the next first point");
    QVERIFY(child<QLabel>("measureResult")->text().startsWith(QStringLiteral("Longueur")));
}

QTEST_MAIN(TestVueWindow)
#include "test_vue_window.moc"
