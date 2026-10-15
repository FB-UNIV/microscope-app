#pragma once

#include <QMainWindow>

namespace msapp {

// The capture app's main window. Empty for now: live preview and capture
// driving a CaptureStation come next (README, next steps).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace msapp
