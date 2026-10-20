#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QPainter;
class QVariantAnimation;

namespace msapp {

// The live preview: the latest frame scaled to fit, aspect ratio kept, on
// the canvas colour — or a message while there is no live frame to show.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    // Replaces the frame: a frozen last frame would pass for a live one.
    // The text before a blank line is the title, the rest its details.
    void showMessage(const QString& message);
    // The same, with a spinner: something is on its way.
    void showBusy(const QString& message);
    // A short white flash over the frame: the capture was taken.
    void flash();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintMessage(QPainter& painter);

    QImage frame_;
    QString message_;
    QVariantAnimation* spinner_;  // runs only while busy, with no frame
    QVariantAnimation* flash_;
};

} // namespace msapp
