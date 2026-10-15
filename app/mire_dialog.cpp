#include "mire_dialog.hpp"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <exception>

namespace msapp {

namespace {

QString percent(double fraction) {
    return QString::number(fraction * 100.0, 'f', 2) + QStringLiteral(" %");
}

QPen markPen() {
    QPen pen(Qt::red);
    pen.setCosmetic(true);  // same width on screen at any zoom
    pen.setWidth(2);
    return pen;
}

} // namespace

MireDialog::MireDialog(const QStringList& grossissements, const QString& current, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Calibrer un grossissement sur la mire de 20 µm"));
    resize(1200, 800);

    grossissement_ = new QComboBox;
    grossissement_->setEditable(true);  // an objective not in the list, e.g. x63
    grossissement_->addItems(grossissements);
    grossissement_->setCurrentText(current);
    periods_ = new QSpinBox;
    periods_->setRange(1, 1000);
    periods_->setValue(10);
    periods_->setSuffix(tr(" périodes"));

    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setBackgroundBrush(Qt::black);
    view_->viewport()->installEventFilter(this);

    readingsList_ = new QListWidget;
    status_ = new QLabel;
    status_->setWordWrap(true);
    newStill_ = new QPushButton(tr("Nouvelle image de la mire"));
    save_ = new QPushButton(tr("Enregistrer la Calibration"));
    save_->setEnabled(false);
    auto* cancel = new QPushButton(tr("Annuler"));

    auto* form = new QFormLayout;
    form->addRow(tr("Grossissement"), grossissement_);
    form->addRow(tr("Entre les deux clics"), periods_);

    auto* help = new QLabel(tr("Molette : zoom. Glisser : déplacer. Cliquez un trait de la mire, puis le trait "
                               "situé autant de périodes de 20 µm plus loin, le plus loin possible. Recommencez "
                               "jusqu'à ce que deux lectures s'accordent à 0,25 %."));
    help->setWordWrap(true);

    auto* side = new QWidget;
    side->setFixedWidth(300);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->addLayout(form);
    sideLayout->addWidget(help);
    sideLayout->addWidget(new QLabel(tr("Lectures (px/µm)")));
    sideLayout->addWidget(readingsList_, 1);
    sideLayout->addWidget(status_);
    sideLayout->addWidget(newStill_);
    sideLayout->addWidget(save_);
    sideLayout->addWidget(cancel);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(view_, 1);
    layout->addWidget(side);

    connect(newStill_, &QPushButton::clicked, this, [this] {
        status_->setText(tr("Prise d'une image…"));
        emit stillRequested();
    });
    connect(grossissement_, &QComboBox::currentTextChanged, this, [this] { restart(); });
    connect(periods_, &QSpinBox::valueChanged, this, [this] { refreshStatus(); });
    connect(save_, &QPushButton::clicked, this, [this] {
        if (mire_ && mire_->agreed()) {
            result_ = mire_->result();
            accept();
        }
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    status_->setText(tr("Prise d'une image…"));
}

void MireDialog::setStill(const QImage& frame) {
    clearMarks();
    if (still_ == nullptr) {
        still_ = scene_->addPixmap(QPixmap::fromImage(frame));
    } else {
        still_->setPixmap(QPixmap::fromImage(frame));
    }
    scene_->setSceneRect(still_->boundingRect());
    view_->fitInView(still_, Qt::KeepAspectRatio);
    frameSize_ = frame.size();
    restart();
}

void MireDialog::stillFailed(const QString& message) {
    status_->setText(tr("Pas d'image de la caméra : %1").arg(message));
}

void MireDialog::restart() {
    clearMarks();
    firstClick_.reset();
    readingsList_->clear();
    mire_.reset();
    const std::string grossissement = grossissement_->currentText().trimmed().toStdString();
    if (frameSize_.isValid() && !grossissement.empty()) {
        mire_.emplace(grossissement, frameSize_.width(), frameSize_.height());
    }
    refreshStatus();
}

void MireDialog::clearMarks() {
    for (QGraphicsItem* item : scene_->items()) {
        if (item != still_) {
            scene_->removeItem(item);
            delete item;
        }
    }
}

void MireDialog::click(QPointF point) {
    if (!mire_ || still_ == nullptr || !still_->boundingRect().contains(point)) return;
    const qreal radius = std::max<qreal>(4.0, frameSize_.width() / 500.0);
    scene_->addEllipse(point.x() - radius, point.y() - radius, 2 * radius, 2 * radius, markPen());
    if (!firstClick_) {
        firstClick_ = point;
        refreshStatus();
        return;
    }
    scene_->addLine(QLineF(*firstClick_, point), markPen());
    try {
        const double pixelsPerMicron =
            mire_->addReading({firstClick_->x(), firstClick_->y()}, {point.x(), point.y()}, periods_->value());
        readingsList_->addItem(QString::number(pixelsPerMicron, 'f', 4));
        readingsList_->scrollToBottom();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Lecture refusée"), QString::fromStdString(e.what()));
    }
    firstClick_.reset();
    refreshStatus();
}

void MireDialog::refreshStatus() {
    save_->setEnabled(mire_ && mire_->agreed());
    if (!mire_) {
        status_->setText(frameSize_.isValid() ? tr("Indiquez le grossissement.") : tr("En attente d'une image…"));
        return;
    }
    const int periods = periods_->value();
    if (firstClick_) {
        status_->setText(tr("Cliquez le trait situé %n période(s) plus loin.", nullptr, periods));
        return;
    }
    const auto& readings = mire_->readings();
    const auto difference = mire_->lastDifference();
    if (readings.empty()) {
        status_->setText(tr("Cliquez un trait de la mire, puis celui situé %n période(s) plus loin.", nullptr, periods));
    } else if (!difference) {
        status_->setText(tr("Première lecture : %1 px/µm. Mesurez une deuxième fois.").arg(readings.back(), 0, 'f', 4));
    } else if (mire_->agreed()) {
        const Calibration calibration = mire_->result();
        status_->setText(tr("Les deux dernières lectures s'accordent (écart %1) : %2 px/µm, image %3x%4.")
                             .arg(percent(*difference))
                             .arg(calibration.pixelsPerMicron, 0, 'f', 4)
                             .arg(calibration.frameWidth)
                             .arg(calibration.frameHeight));
    } else {
        status_->setText(tr("Écart de %1 avec la lecture précédente, plus de 0,25 % : mesurez encore.")
                             .arg(percent(*difference)));
    }
}

bool MireDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_->viewport()) {
        if (event->type() == QEvent::Wheel) {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const double factor = wheel->angleDelta().y() > 0 ? 1.25 : 0.8;
            view_->scale(factor, factor);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) pressPosition_ = mouse->position().toPoint();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint position = mouse->position().toPoint();
            // A click, not the end of a drag that panned the view.
            if (mouse->button() == Qt::LeftButton && (position - pressPosition_).manhattanLength() < 4) {
                click(view_->mapToScene(position));
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace msapp
