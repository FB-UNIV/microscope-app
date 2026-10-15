#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

namespace msapp {

// The live preview: the latest frame scaled to fit, aspect ratio kept, on
// black — or a message while there is no live frame to show.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    // Replaces the frame: a frozen last frame would pass for a live one.
    void showMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage frame_;
    QString message_;
};

} // namespace msapp
