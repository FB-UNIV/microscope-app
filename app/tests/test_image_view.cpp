// User journey:
//   As a student measuring on a Vue, I click a point on the photo and the
//   tool gets it in image pixels; I drag the photo to pan without leaving a
//   point behind; I grab a yellow point near (not exactly on) it and drag it
//   to correct it; I right-click a point to delete its measurement; with the
//   zone tool I drag a rectangle, even past the photo's edge; and two quick
//   clicks (four Vickers corners in a row) count as two clicks (#56).
//
// QtTest, run offscreen by CTest (QT_QPA_PLATFORM=offscreen): the core
// tests stay Qt-free, this one only builds with the app.

#include "image_view.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>

using msapp::ImageView;

namespace {

// The photo: 200x150, fitted (about 2x) into a 420x320 view.
constexpr int kImageWidth = 200;
constexpr int kImageHeight = 150;

bool near(QPointF a, QPointF b, double tolerance) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

// QTest has no mouse move with a button held on every Qt version, and its
// double-click does not send the release in between that a real one does:
// these send exactly what the platform would.
void send(QWidget* viewport, QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons held) {
    QMouseEvent event(type, QPointF(at), QPointF(viewport->mapToGlobal(at)), button, held, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
}

void moveTo(QWidget* viewport, QPoint at, Qt::MouseButtons held) {
    send(viewport, QEvent::MouseMove, at, Qt::NoButton, held);
}

} // namespace

class TestImageView : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void clickReportsTheImagePoint();
    void clickOutsideTheImageIsIgnored();
    void leftDragPansWithoutClicking();
    void dragNearAHandleMovesIt();
    void rightClickOnAHandleAsksToRemoveIt();
    void rectangleModeDragReportsTheRectangleInsideTheImage();
    void doubleClickIsTwoClicks();
    void zoomKeysFitShowActualSizeAndStep();
    void resizeKeepsTheFitUntilZoomed();
    void hoverReportsThePointAndTheLoupeFollows();
    void guideShowsItsLengthUntilMarksAreCleared();

private:
    // Where image pixel `imagePoint` is on the viewport.
    QPoint onScreen(QPointF imagePoint) const { return view_->mapFromScene(imagePoint); }
    QWidget* viewport() const { return view_->viewport(); }

    ImageView* view_ = nullptr;
};

void TestImageView::init() {
    view_ = new ImageView;
    view_->resize(420, 320);
    view_->show();
    QVERIFY(QTest::qWaitForWindowExposed(view_));
    QImage image(kImageWidth, kImageHeight, QImage::Format_RGB32);
    image.fill(Qt::gray);
    view_->setImage(image);
    QCOMPARE(view_->imageSize(), QSize(kImageWidth, kImageHeight));
}

void TestImageView::cleanup() {
    delete view_;
    view_ = nullptr;
}

void TestImageView::clickReportsTheImagePoint() {
    QSignalSpy clicked(view_, &ImageView::clicked);
    QTest::mouseClick(viewport(), Qt::LeftButton, Qt::NoModifier, onScreen(QPointF(120, 40)));
    QCOMPARE(clicked.count(), 1);
    const QPointF point = clicked.at(0).at(0).toPointF();
    QVERIFY2(near(point, QPointF(120, 40), 1.0),
             qPrintable(QStringLiteral("clicked at (%1, %2)").arg(point.x()).arg(point.y())));
}

void TestImageView::clickOutsideTheImageIsIgnored() {
    view_->scale(0.5, 0.5);  // the photo now leaves a black margin around it
    QCoreApplication::processEvents();
    QSignalSpy clicked(view_, &ImageView::clicked);
    QTest::mouseClick(viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(clicked.count(), 0);
}

void TestImageView::leftDragPansWithoutClicking() {
    view_->scale(4, 4);  // zoomed in: the scroll bars have room
    QCoreApplication::processEvents();  // scroll bars shown, viewport laid out again
    view_->centerOn(QPointF(kImageWidth / 2.0, kImageHeight / 2.0));
    QCoreApplication::processEvents();
    QScrollBar* horizontal = view_->horizontalScrollBar();
    QScrollBar* vertical = view_->verticalScrollBar();
    const int x = horizontal->value();
    const int y = vertical->value();

    QSignalSpy clicked(view_, &ImageView::clicked);
    const QPoint start(200, 150);
    QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, start);
    moveTo(viewport(), start + QPoint(15, 10), Qt::LeftButton);
    moveTo(viewport(), start + QPoint(30, 20), Qt::LeftButton);
    QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(30, 20));

    QCOMPARE(horizontal->value(), x - 30);  // the photo follows the mouse
    QCOMPARE(vertical->value(), y - 20);
    QCOMPARE(clicked.count(), 0);
}

void TestImageView::dragNearAHandleMovesIt() {
    view_->setHandles({QPointF(50, 50), QPointF(150, 100)});
    QSignalSpy moved(view_, &ImageView::handleMoved);
    QSignalSpy clicked(view_, &ImageView::clicked);

    const QPoint grab = onScreen(QPointF(150, 100)) + QPoint(6, 0);  // within the 9 px grab radius
    const QPoint drop = onScreen(QPointF(120, 80)) + QPoint(6, 0);
    QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, grab);
    moveTo(viewport(), drop, Qt::LeftButton);
    QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, drop);

    QVERIFY(moved.count() >= 1);
    const QList<QVariant> last = moved.last();
    QCOMPARE(last.at(0).toInt(), 1);
    QVERIFY(near(last.at(1).toPointF(), view_->mapToScene(drop), 0.01));
    QCOMPARE(clicked.count(), 0);
}

void TestImageView::rightClickOnAHandleAsksToRemoveIt() {
    view_->setHandles({QPointF(50, 50), QPointF(150, 100)});
    QSignalSpy remove(view_, &ImageView::handleRemoveRequested);
    QSignalSpy clicked(view_, &ImageView::clicked);

    QTest::mouseClick(viewport(), Qt::RightButton, Qt::NoModifier, onScreen(QPointF(50, 50)) + QPoint(0, 4));
    QCOMPARE(remove.count(), 1);
    QCOMPARE(remove.at(0).at(0).toInt(), 0);

    QTest::mouseClick(viewport(), Qt::RightButton, Qt::NoModifier, onScreen(QPointF(100, 20)));
    QCOMPARE(remove.count(), 1);  // far from every handle: nothing to remove
    QCOMPARE(clicked.count(), 0);
}

void TestImageView::rectangleModeDragReportsTheRectangleInsideTheImage() {
    view_->scale(0.5, 0.5);  // a black margin around the photo, to drag into
    QCoreApplication::processEvents();
    view_->setRectangleMode(true);
    QSignalSpy dragged(view_, &ImageView::rectangleDragged);
    QSignalSpy clicked(view_, &ImageView::clicked);
    const double tolerance = 1.0 / view_->transform().m11() + 0.01;  // one screen pixel

    // From inside, up and left past the photo's corner.
    const QPoint start = onScreen(QPointF(150, 100));
    QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, start);
    moveTo(viewport(), start - QPoint(20, 20), Qt::LeftButton);
    moveTo(viewport(), QPoint(1, 1), Qt::LeftButton);
    QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));

    QCOMPARE(dragged.count(), 1);
    const QRectF rect = dragged.at(0).at(0).toRectF();
    QVERIFY2(near(rect.topLeft(), QPointF(0, 0), 0.01) && near(rect.bottomRight(), QPointF(150, 100), tolerance),
             qPrintable(QStringLiteral("rectangle (%1, %2) %3x%4")
                            .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height())));
    QCOMPARE(clicked.count(), 0);
}

void TestImageView::doubleClickIsTwoClicks() {
    QSignalSpy clicked(view_, &ImageView::clicked);
    const QPoint at = onScreen(QPointF(80, 60));
    QTest::mousePress(viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(viewport(), Qt::LeftButton, Qt::NoModifier, at);
    send(viewport(), QEvent::MouseButtonDblClick, at, Qt::LeftButton, Qt::LeftButton);
    send(viewport(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(clicked.count(), 2);
}

// #90: 0 fits, 1 shows one image pixel per screen pixel, + and − step.
void TestImageView::zoomKeysFitShowActualSizeAndStep() {
    QSignalSpy zoomed(view_, &ImageView::zoomChanged);
    const double fitted = view_->zoom();
    QVERIFY2(fitted > 1.5 && fitted < 2.2, qPrintable(QString::number(fitted)));  // 200x150 in 420x320

    QTest::keyClick(view_, Qt::Key_1);
    QVERIFY(qFuzzyCompare(view_->zoom(), 1.0));
    QTest::keyClick(view_, Qt::Key_Plus);
    QVERIFY(qFuzzyCompare(view_->zoom(), 1.25));
    QTest::keyClick(view_, Qt::Key_Minus);
    QVERIFY(qFuzzyCompare(view_->zoom(), 1.0));
    QTest::keyClick(view_, Qt::Key_0);
    QVERIFY(qFuzzyCompare(view_->zoom(), fitted));
    QCOMPARE(zoomed.count(), 4);
    QCOMPARE(zoomed.last().at(0).toDouble(), view_->zoom());
}

void TestImageView::resizeKeepsTheFitUntilZoomed() {
    const double fitted = view_->zoom();
    view_->resize(840, 640);
    QTRY_VERIFY(view_->zoom() > fitted * 1.5);  // still the whole photo, now bigger

    view_->zoomBy(2.0);
    const double zoomed = view_->zoom();
    view_->resize(420, 320);
    QCoreApplication::processEvents();
    QCOMPARE(view_->zoom(), zoomed);  // zoomed in: a resize leaves it alone
}

void TestImageView::hoverReportsThePointAndTheLoupeFollows() {
    QSignalSpy hovered(view_, &ImageView::hovered);
    QSignalSpy unhovered(view_, &ImageView::unhovered);
    view_->setLoupe(true);
    moveTo(viewport(), onScreen(QPointF(50, 60)), Qt::NoButton);
    QCOMPARE(hovered.count(), 1);
    QVERIFY(near(hovered.at(0).at(0).toPointF(), QPointF(50, 60), 1.0));
    QVERIFY(!view_->grab().isNull());  // paints the loupe over the photo

    view_->scale(0.5, 0.5);  // a margin around the photo
    QCoreApplication::processEvents();
    moveTo(viewport(), QPoint(2, 2), Qt::NoButton);
    QCOMPARE(unhovered.count(), 1);
}

void TestImageView::guideShowsItsLengthUntilMarksAreCleared() {
    view_->setGuide(QLineF(QPointF(10, 10), QPointF(60, 10)), QStringLiteral("12.5 µm"));
    QCOMPARE(view_->guideLabel(), QStringLiteral("12.5 µm"));
    view_->setPrompt(QStringLiteral("Longueur — point 2/2"));
    QVERIFY(!view_->grab().isNull());  // paints the prompt
    view_->clearMarks();
    QVERIFY(view_->guideLabel().isEmpty());
    view_->setGuide(std::nullopt);
    QVERIFY(view_->guideLabel().isEmpty());
}

QTEST_MAIN(TestImageView)
#include "test_image_view.moc"
