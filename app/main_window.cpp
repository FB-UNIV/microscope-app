#include "main_window.hpp"

#include <QGuiApplication>
#include <QLabel>
#include <QStatusBar>

#include <opencv2/core/utility.hpp>

namespace msapp {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QGuiApplication::applicationDisplayName());
    resize(1024, 768);
    // The OpenCV the app runs on, as camera_probe reports it. It also makes
    // the exe load the OpenCV DLL at start, so the CI launch check of the
    // Windows bundle proves the bundle ships it.
    statusBar()->addPermanentWidget(
        new QLabel(QStringLiteral("OpenCV %1").arg(QString::fromStdString(cv::getVersionString()))));
}

} // namespace msapp
