#include "mire_dialog.hpp"

#include "image_view.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <exception>

namespace msapp {

namespace {

QString percent(double fraction) {
    return QString::number(fraction * 100.0, 'f', 2) + QStringLiteral(" %");
}

} // namespace

MireDialog::MireDialog(const QStringList& grossissements, const QString& current, double pitchMicrons,
                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Calibrer un grossissement sur la mire"));
    resize(1200, 800);

    grossissement_ = new QComboBox;
    grossissement_->setEditable(true);  // an objective not in the list, e.g. x63
    grossissement_->addItems(grossissements);
    grossissement_->setCurrentText(current);
    pitch_ = new QDoubleSpinBox;
    pitch_->setRange(0.1, 1000.0);
    pitch_->setDecimals(2);
    pitch_->setSuffix(tr(" µm"));
    pitch_->setValue(pitchMicrons);
    pitch_->setToolTip(tr("La distance réelle entre deux traits voisins de la mire : 20 µm pour la mire du labo, "
                          "10 µm pour un micromètre de 1 mm en 100 divisions."));
    periods_ = new QSpinBox;
    periods_->setRange(1, 1000);
    periods_->setValue(10);
    periods_->setSuffix(tr(" périodes"));
    periods_->setToolTip(tr("Le nombre d'intervalles entre les deux traits cliqués, pas le nombre de traits : "
                            "5 traits = 4 périodes."));

    view_ = new ImageView;
    readingsList_ = new QListWidget;
    direction_ = new QLabel;
    direction_->setWordWrap(true);
    status_ = new QLabel;
    status_->setWordWrap(true);
    auto* newStill = new QPushButton(tr("Nouvelle image de la mire"));
    save_ = new QPushButton(tr("Enregistrer la Calibration"));
    save_->setEnabled(false);
    auto* cancel = new QPushButton(tr("Annuler"));

    auto* form = new QFormLayout;
    form->addRow(tr("Grossissement"), grossissement_);
    form->addRow(tr("Période de la mire"), pitch_);
    form->addRow(tr("Entre les deux clics"), periods_);

    auto* help = new QLabel(tr("Molette : zoom. Glisser : déplacer. Cliquez un trait de la mire, puis le trait "
                               "situé autant de périodes plus loin (une période = l'intervalle entre deux traits "
                               "voisins), le plus loin possible. Recommencez jusqu'à ce que deux lectures "
                               "s'accordent à 0,25 %.\n\nUn seul objectif suffit : "
                               "les autres grossissements en sont déduits. Calibrer aussi un autre objectif sur la "
                               "mire le rend plus précis ; les autres sont alors déduits du dernier calibré."));
    help->setWordWrap(true);

    auto* side = new QWidget;
    side->setFixedWidth(300);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->addLayout(form);
    sideLayout->addWidget(help);
    sideLayout->addWidget(direction_);
    sideLayout->addWidget(new QLabel(tr("Lectures (px/µm)")));
    sideLayout->addWidget(readingsList_, 1);
    sideLayout->addWidget(status_);
    sideLayout->addWidget(newStill);
    sideLayout->addWidget(save_);
    sideLayout->addWidget(cancel);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(view_, 1);
    layout->addWidget(side);

    connect(view_, &ImageView::clicked, this, &MireDialog::click);
    connect(newStill, &QPushButton::clicked, this, [this] {
        status_->setText(tr("Prise d'une image…"));
        emit stillRequested();
    });
    connect(grossissement_, &QComboBox::currentTextChanged, this, [this] { restart(); });
    // Readings so far were converted with the old period.
    connect(pitch_, &QDoubleSpinBox::valueChanged, this, [this] { restart(); });
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

void MireDialog::setStill(const QImage& frame, std::optional<cv::Vec2d> lineNormal) {
    view_->setImage(frame);
    frameSize_ = frame.size();
    lineNormal_ = lineNormal;
    if (lineNormal_) {
        // Tilt of the lines from vertical, for the operator to check.
        double tilt = std::atan2((*lineNormal_)[1], (*lineNormal_)[0]) * 180.0 / CV_PI;
        if (tilt > 90.0) tilt -= 180.0;
        if (tilt < -90.0) tilt += 180.0;
        direction_->setText(tr("Traits détectés, inclinés de %1° : cliquez à n'importe quelle hauteur, seule la "
                               "distance entre les deux traits compte.")
                                .arg(tilt, 0, 'f', 1));
    } else {
        direction_->setText(tr("Direction des traits non trouvée dans l'image : cliquez les deux traits à la même "
                               "hauteur."));
    }
    restart();
}

double MireDialog::pitchMicrons() const {
    return pitch_->value();
}

void MireDialog::stillFailed(const QString& message) {
    status_->setText(tr("Pas d'image de la caméra : %1").arg(message));
}

void MireDialog::restart() {
    view_->clearMarks();
    firstClick_.reset();
    readingsList_->clear();
    mire_.reset();
    const std::string grossissement = grossissement_->currentText().trimmed().toStdString();
    if (frameSize_.isValid() && !grossissement.empty()) {
        mire_.emplace(grossissement, frameSize_.width(), frameSize_.height(), pitch_->value());
        mire_->setLineNormal(lineNormal_);
    }
    refreshStatus();
}

void MireDialog::click(QPointF point) {
    if (!mire_) return;
    view_->addPoint(point);
    if (!firstClick_) {
        firstClick_ = point;
        refreshStatus();
        return;
    }
    // What is measured: from the first click straight across the lines, to
    // the second click's line.
    QPointF across = point;
    if (lineNormal_) {
        const QPointF normal((*lineNormal_)[0], (*lineNormal_)[1]);
        const QPointF step = point - *firstClick_;
        across = *firstClick_ + normal * (step.x() * normal.x() + step.y() * normal.y());
    }
    view_->addLine(*firstClick_, across);
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

} // namespace msapp
