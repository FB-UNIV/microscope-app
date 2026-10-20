#include "camera_setup_dialog.hpp"

#include "preview_widget.hpp"
#include "theme.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace msapp {

namespace {

constexpr int kPixelFormatRole = Qt::UserRole + 1;
constexpr double kA5Kelvin = 5000.0;

QString apiLabel(const std::string& api) {
    if (api == "dshow") return QStringLiteral("DirectShow");
    if (api == "msmf") return QStringLiteral("Media Foundation");
    if (api == "v4l2") return QStringLiteral("V4L2");
    if (api == "auto") return CameraSetupDialog::tr("automatique");
    return QString::fromStdString(api);
}

QString modeText(int width, int height, const std::string& pixelFormat) {
    if (width <= 0 || height <= 0) return CameraSetupDialog::tr("mode par défaut de la caméra");
    QString text = QStringLiteral("%1×%2").arg(width).arg(height);
    if (!pixelFormat.empty()) text += QLatin1Char(' ') + QString::fromStdString(pixelFormat);
    return text;
}

QString grantedModeText(const ProfileReport& report) {
    QString text = report.frameSize.empty()
                       ? CameraSetupDialog::tr("taille inconnue")
                       : QStringLiteral("%1×%2").arg(report.frameSize.width).arg(report.frameSize.height);
    if (!report.pixelFormat.empty()) text += QLatin1Char(' ') + QString::fromStdString(report.pixelFormat);
    return text;
}

// "7.81 ms (1/128 s)": DirectShow's steps are whole fractions of a second.
QString exposureText(double ms) {
    const int decimals = ms >= 100.0 ? 0 : ms >= 10.0 ? 1 : ms >= 1.0 ? 2 : 3;
    QString text = QStringLiteral("%1 ms").arg(ms, 0, 'f', decimals);
    const double perSecond = 1000.0 / ms;
    const long rounded = std::lround(perSecond);
    if (rounded >= 2 && std::abs(perSecond - static_cast<double>(rounded)) < 1e-6) {
        text += QStringLiteral(" (1/%1 s)").arg(rounded);
    }
    return text;
}

QString gainsText(const WhiteBalance& gains) {
    return CameraSetupDialog::tr("rouge %1, vert %2, bleu %3")
        .arg(gains.red, 0, 'f', 3)
        .arg(gains.green, 0, 'f', 3)
        .arg(gains.blue, 0, 'f', 3);
}

QString cameraText(const CameraConfig& camera) {
    if (camera.backend != "opencv") return CameraSetupDialog::tr("relecture d'images");
    return CameraSetupDialog::tr("caméra %1 (%2)").arg(camera.deviceIndex).arg(apiLabel(camera.captureApi));
}

QLabel* wrapped(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    return label;
}

// A note under a page's controls, quieter than its status.
QLabel* note(const QString& text) {
    QLabel* label = wrapped(text);
    theme::setRole(label, "role", QStringLiteral("muted"));
    return label;
}

// Where the wizard is: one segment per step, copper up to the current one.
class StepStrip : public QWidget {
public:
    explicit StepStrip(int count, QWidget* parent = nullptr) : QWidget(parent), count_(count) {
        setFixedHeight(4);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setCurrent(int current) {
        current_ = current;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        constexpr double gap = 6.0;
        const double segment = (width() - gap * (count_ - 1)) / count_;
        for (int i = 0; i < count_; ++i) {
            const QColor colour =
                i < current_ ? theme::accent.darker(140) : i == current_ ? theme::accent : theme::border;
            painter.setBrush(colour);
            painter.drawRoundedRect(QRectF(i * (segment + gap), 0, segment, height()), 2, 2);
        }
    }

private:
    int count_;
    int current_ = 0;
};

} // namespace

CameraSetupDialog::CameraSetupDialog(WorkstationConfig saved, CameraConfig start, std::vector<std::string> captureApis,
                                     QWidget* parent)
    : QDialog(parent), saved_(std::move(saved)), proposed_(std::move(start)), captureApis_(std::move(captureApis)) {
    setWindowTitle(tr("Configurer la caméra"));
    setObjectName(QStringLiteral("cameraSetupDialog"));
    resize(1280, 720);

    preview_ = new PreviewWidget;
    preview_->showBusy(tr("Ouverture de la caméra…"));
    liveStatus_ = wrapped(QString());
    theme::setRole(liveStatus_, "role", QStringLiteral("muted"));
    stepStrip_ = new StepStrip(PageCount);
    stepTitle_ = new QLabel;
    theme::setRole(stepTitle_, "role", QStringLiteral("title"));
    pages_ = new QStackedWidget;

    // 1. The camera.
    {
        auto* page = new QWidget;
        cameraList_ = new QListWidget;
        cameraList_->setObjectName(QStringLiteral("cameraList"));
        search_ = new QPushButton(tr("Rechercher à nouveau"));
        searchStatus_ = wrapped(tr("Recherche des caméras…"));
        searchStatus_->setObjectName(QStringLiteral("searchStatus"));
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(note(tr("Choisissez la Moticam A5 par DirectShow : seule cette interface règle sa "
                                  "température de couleur. Fermez MotiConnect avant de chercher.")));
        layout->addWidget(cameraList_, 1);
        layout->addWidget(searchStatus_);
        layout->addWidget(search_, 0, Qt::AlignLeft);
        pages_->addWidget(page);
    }
    // 2. Its mode.
    {
        auto* page = new QWidget;
        api_ = new QComboBox;
        api_->setObjectName(QStringLiteral("captureApi"));
        for (const std::string& api : captureApis_) api_->addItem(apiLabel(api), QString::fromStdString(api));
        if (api_->findData(QStringLiteral("auto")) < 0) {
            api_->addItem(tr("automatique (OpenCV choisit)"), QStringLiteral("auto"));
        }
        mode_ = new QComboBox;
        mode_->setObjectName(QStringLiteral("cameraMode"));
        const CameraConfig a5 = moticamA5Profile();
        for (const CameraMode& mode : commonCameraModes()) {
            QString label = modeText(mode.width, mode.height, mode.pixelFormat);
            if (mode.width == a5.width && mode.height == a5.height && mode.pixelFormat == a5.pixelFormat) {
                label += tr(" — profil Moticam A5");
            }
            if (mode.width <= 0) label = tr("Mode par défaut de la caméra");
            mode_->addItem(label, QSize(mode.width, mode.height));
            mode_->setItemData(mode_->count() - 1, QString::fromStdString(mode.pixelFormat), kPixelFormatRole);
        }
        granted_ = wrapped(QString());
        granted_->setObjectName(QStringLiteral("grantedMode"));
        calibrations_ = wrapped(QString());
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(new QLabel(tr("Interface de capture")));
        layout->addWidget(api_);
        layout->addWidget(new QLabel(tr("Taille d'image et format")));
        layout->addWidget(mode_);
        layout->addSpacing(12);
        layout->addWidget(granted_);
        layout->addSpacing(12);
        layout->addWidget(calibrations_);
        layout->addSpacing(12);
        layout->addWidget(note(tr("Une Calibration ne vaut que pour la taille d'image où elle a été mesurée.")));
        layout->addStretch(1);
        pages_->addWidget(page);
    }
    // 3. Exposure.
    {
        auto* page = new QWidget;
        autoExposure_ = new QRadioButton(tr("Automatique"));
        fixedExposure_ = new QRadioButton(tr("Fixe :"));
        fixedExposure_->setObjectName(QStringLiteral("fixedExposure"));
        auto* group = new QButtonGroup(page);
        group->addButton(autoExposure_);
        group->addButton(fixedExposure_);
        exposure_ = new QComboBox;
        exposure_->setObjectName(QStringLiteral("exposure"));
        for (const double ms : exposureStepsMs()) exposure_->addItem(exposureText(ms), ms);
        exposureStatus_ = wrapped(QString());
        auto* fixedRow = new QHBoxLayout;
        fixedRow->addWidget(fixedExposure_);
        fixedRow->addWidget(exposure_, 1);
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(autoExposure_);
        layout->addLayout(fixedRow);
        layout->addSpacing(12);
        layout->addWidget(exposureStatus_);
        layout->addSpacing(12);
        layout->addWidget(note(tr("Profil Moticam A5 : 7,81 ms (1/128 s). Réglez ensuite la luminosité avec la "
                                  "lampe.")));
        layout->addStretch(1);
        pages_->addWidget(page);
    }
    // 4. Colour temperature.
    {
        auto* page = new QWidget;
        autoColour_ = new QRadioButton(tr("Automatique (déconseillé : les couleurs dérivent)"));
        lockedColour_ = new QRadioButton(tr("Verrouillée :"));
        lockedColour_->setObjectName(QStringLiteral("lockedColour"));
        auto* group = new QButtonGroup(page);
        group->addButton(autoColour_);
        group->addButton(lockedColour_);
        kelvin_ = new QSpinBox;
        kelvin_->setObjectName(QStringLiteral("kelvin"));
        kelvin_->setRange(2000, 10000);
        kelvin_->setSingleStep(100);
        kelvin_->setSuffix(QStringLiteral(" K"));
        kelvin_->setKeyboardTracking(false);  // a typed value goes to the camera once, not per digit
        colourStatus_ = wrapped(QString());
        auto* lockedRow = new QHBoxLayout;
        lockedRow->addWidget(lockedColour_);
        lockedRow->addWidget(kelvin_, 1);
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(autoColour_);
        layout->addLayout(lockedRow);
        layout->addSpacing(12);
        layout->addWidget(colourStatus_);
        layout->addSpacing(12);
        layout->addWidget(note(tr("Profil Moticam A5 : 5000 K. La référence blanche, à l'étape suivante, corrige la "
                                  "teinte restante.")));
        layout->addStretch(1);
        pages_->addWidget(page);
    }
    // 5. White reference.
    {
        auto* page = new QWidget;
        whiteStatus_ = wrapped(QString());
        measureWhite_ = new QPushButton(tr("Mesurer la référence blanche"));
        measureWhite_->setObjectName(QStringLiteral("measureWhite"));
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(note(tr("Retirez l'échantillon, allumez la lampe, visez un champ vide et uniforme, puis "
                                  "mesurez.")));
        layout->addSpacing(12);
        layout->addWidget(whiteStatus_);
        layout->addWidget(measureWhite_, 0, Qt::AlignLeft);
        layout->addStretch(1);
        pages_->addWidget(page);
    }
    // 6. What saving means.
    {
        auto* page = new QWidget;
        summary_ = wrapped(QString());
        summary_->setObjectName(QStringLiteral("summary"));
        recalibrate_ = new QCheckBox(tr("Calibrer un grossissement sur la mire juste après"));
        recalibrate_->setObjectName(QStringLiteral("recalibrate"));
        recalibrate_->setChecked(true);
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(summary_);
        layout->addSpacing(12);
        layout->addWidget(recalibrate_);
        layout->addStretch(1);
        pages_->addWidget(page);
    }

    auto* restore = new QPushButton(tr("Rétablir le profil Moticam A5"));
    restore->setObjectName(QStringLiteral("restoreA5"));
    restore->setToolTip(tr("DirectShow, 3840×2160 MJPG, exposition 7,81 ms, 5000 K. La référence blanche est gardée."));
    theme::setRole(restore, "variant", QStringLiteral("quiet"));
    auto* cancel = new QPushButton(tr("Annuler"));
    back_ = new QPushButton(tr("‹ Précédent"));
    next_ = new QPushButton(tr("Suivant ›"));
    next_->setObjectName(QStringLiteral("nextButton"));
    theme::setRole(next_, "variant", QStringLiteral("primary"));
    save_ = new QPushButton(tr("Enregistrer"));
    save_->setObjectName(QStringLiteral("saveButton"));
    theme::setRole(save_, "variant", QStringLiteral("primary"));
    for (QPushButton* button : {restore, cancel, back_, save_}) button->setAutoDefault(false);
    next_->setDefault(true);

    auto* left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(preview_, 1);
    left->addWidget(liveStatus_);
    auto* right = new QVBoxLayout;
    right->setSpacing(10);
    right->addWidget(stepStrip_);
    right->addSpacing(4);
    right->addWidget(stepTitle_);
    right->addWidget(pages_, 1);
    auto* top = new QHBoxLayout;
    top->setSpacing(20);
    top->addLayout(left, 3);
    top->addLayout(right, 2);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(restore);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(back_);
    buttons->addWidget(next_);
    buttons->addWidget(save_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);
    layout->addLayout(top, 1);
    layout->addLayout(buttons);

    const auto search = [this] {
        searching_ = true;
        searchStatus_->setText(tr("Recherche des caméras…"));
        refreshStatus();
        emit searchRequested();
    };
    connect(search_, &QPushButton::clicked, this, search);
    connect(cameraList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= static_cast<int>(cameras_.size())) return;
        const FoundCamera& camera = cameras_[static_cast<std::size_t>(row)];
        CameraConfig next = proposed_;
        next.backend = "opencv";
        next.deviceIndex = camera.deviceIndex;
        next.captureApi = camera.captureApi;
        propose(next, true);
    });
    connect(api_, &QComboBox::currentIndexChanged, this, [this] {
        CameraConfig next = proposed_;
        next.captureApi = api_->currentData().toString().toStdString();
        propose(next, true);
    });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        const QSize size = mode_->currentData().toSize();
        CameraConfig next = proposed_;
        next.width = size.width();
        next.height = size.height();
        next.pixelFormat = mode_->currentData(kPixelFormatRole).toString().toStdString();
        propose(next, true);
    });
    const auto exposureChanged = [this] {
        CameraConfig next = proposed_;
        next.exposureMs.reset();
        if (fixedExposure_->isChecked()) next.exposureMs = exposure_->currentData().toDouble();
        propose(next, false);
    };
    connect(fixedExposure_, &QRadioButton::toggled, this, exposureChanged);
    connect(exposure_, &QComboBox::currentIndexChanged, this, exposureChanged);
    const auto colourChanged = [this] {
        CameraConfig next = proposed_;
        next.whiteBalanceKelvin.reset();
        if (lockedColour_->isChecked()) next.whiteBalanceKelvin = static_cast<double>(kelvin_->value());
        propose(next, false);
    };
    connect(lockedColour_, &QRadioButton::toggled, this, colourChanged);
    connect(kelvin_, &QSpinBox::valueChanged, this, colourChanged);
    connect(measureWhite_, &QPushButton::clicked, this, [this] {
        measuringWhite_ = true;
        refreshStatus();
        emit whiteReferenceRequested();
    });
    connect(restore, &QPushButton::clicked, this, &CameraSetupDialog::restoreA5);
    connect(back_, &QPushButton::clicked, this, [this] { goTo(pages_->currentIndex() - 1); });
    connect(next_, &QPushButton::clicked, this, [this] { goTo(pages_->currentIndex() + 1); });
    connect(save_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    showProposal();
    goTo(CameraPage);
    // Once the window has connected its answers: list the cameras at once.
    QTimer::singleShot(0, this, search);
}

void CameraSetupDialog::showMessage(const QString& message) {
    preview_->showBusy(message);  // only "Recherche des caméras…"
}

void CameraSetupDialog::cameraStarting() {
    report_.reset();
    failure_.clear();
    fps_ = 0.0;
    preview_->showBusy(tr("Ouverture de la caméra…"));
    refreshStatus();
}

void CameraSetupDialog::cameraOpened(const ProfileReport& report) {
    report_ = report;
    failure_.clear();
    refreshStatus();
}

void CameraSetupDialog::cameraFailed(const QString& message) {
    report_.reset();
    failure_ = message;
    fps_ = 0.0;
    preview_->showMessage(tr("La caméra ne s'ouvre pas avec ces réglages.\n\n%1").arg(message));
    refreshStatus();
}

void CameraSetupDialog::setFrame(const QImage& frame) {
    preview_->setFrame(frame);
}

void CameraSetupDialog::setFps(double fps) {
    fps_ = fps;
    refreshStatus();
}

void CameraSetupDialog::camerasFound(const std::vector<FoundCamera>& cameras, const QString& error) {
    searching_ = false;
    cameras_ = cameras;
    {
        const QSignalBlocker blocker(cameraList_);
        cameraList_->clear();
        for (const FoundCamera& camera : cameras_) {
            QString text = tr("Caméra %1 — %2 — %3")
                               .arg(camera.deviceIndex)
                               .arg(apiLabel(camera.captureApi))
                               .arg(camera.defaultSize.empty()
                                        ? tr("mode inconnu")
                                        : modeText(camera.defaultSize.width, camera.defaultSize.height,
                                                   camera.defaultPixelFormat));
            if (!camera.deliversFrames) text += tr(" — ⚠ aucune image reçue");
            cameraList_->addItem(text);
        }
    }
    if (!error.isEmpty()) {
        searchStatus_->setText(theme::statusHtml(tr("⚠ Recherche impossible : %1").arg(error.toHtmlEscaped())));
    } else if (cameras_.empty()) {
        searchStatus_->setText(theme::statusHtml(
            tr("⚠ Aucune caméra trouvée. Branchez la Moticam A5, fermez MotiConnect, puis « Rechercher à nouveau ».")));
    } else {
        searchStatus_->setText(tr("%n caméra(s) trouvée(s). Cliquez-en une pour l'essayer.", nullptr,
                                  static_cast<int>(cameras_.size())));
    }
    showProposal();
    refreshStatus();
}

void CameraSetupDialog::controlsApplied(const ProfileReport& report) {
    report_ = report;
    refreshStatus();
}

void CameraSetupDialog::whiteReferenceMeasured(const WhiteBalance& gains) {
    measuringWhite_ = false;
    proposed_.whiteReference = gains;
    whiteMeasured_ = true;
    refreshStatus();
}

void CameraSetupDialog::whiteReferenceFailed(const QString& message) {
    measuringWhite_ = false;
    refreshStatus();
    QMessageBox::warning(this, tr("Champ refusé"), message);
}

CameraSetupReview CameraSetupDialog::review() const {
    return reviewCameraSetup(saved_, proposed_, report_.value_or(ProfileReport()));
}

bool CameraSetupDialog::recalibrateRequested() const {
    return report_ && recalibrate_->isChecked() && review().calibrationsLost();
}

void CameraSetupDialog::goTo(int page) {
    if (page < 0 || page >= PageCount) return;
    pages_->setCurrentIndex(page);
    static const char* const titles[PageCount] = {
        QT_TR_NOOP("Caméra"),     QT_TR_NOOP("Mode"),
        QT_TR_NOOP("Exposition"), QT_TR_NOOP("Température de couleur"),
        QT_TR_NOOP("Référence blanche"), QT_TR_NOOP("Enregistrer"),
    };
    static_cast<StepStrip*>(stepStrip_)->setCurrent(page);
    stepTitle_->setText(tr("%1. %2").arg(page + 1).arg(tr(titles[page])));
    back_->setEnabled(page > 0);
    next_->setVisible(page < SavePage);
    save_->setVisible(page == SavePage);
    if (page == SavePage) save_->setDefault(true);
    refreshStatus();
}

void CameraSetupDialog::propose(CameraConfig next, bool reopen) {
    // A White reference measured here holds only under the settings it was
    // measured with (ADR 0014): changing them drops it, and the preview
    // reopens without its gains.
    if (whiteMeasured_ && !whiteReferenceHolds(proposed_, next)) {
        next.whiteReference = saved_.camera.whiteReference;
        whiteMeasured_ = false;
        reopen = true;
    }
    proposed_ = std::move(next);
    showProposal();
    if (reopen) {
        report_.reset();
        failure_.clear();
        emit profileRequested(proposed_);
    } else {
        emit controlsRequested(proposed_);
    }
    refreshStatus();
}

void CameraSetupDialog::restoreA5() {
    CameraConfig a5 = moticamA5Profile();
    a5.whiteReference = proposed_.whiteReference;  // the microscope's, not the camera's
    propose(a5, true);
}

void CameraSetupDialog::showProposal() {
    const QSignalBlocker blockList(cameraList_);
    const QSignalBlocker blockApi(api_);
    const QSignalBlocker blockMode(mode_);
    const QSignalBlocker blockAutoExposure(autoExposure_);
    const QSignalBlocker blockFixedExposure(fixedExposure_);
    const QSignalBlocker blockExposure(exposure_);
    const QSignalBlocker blockAutoColour(autoColour_);
    const QSignalBlocker blockLockedColour(lockedColour_);
    const QSignalBlocker blockKelvin(kelvin_);

    int row = -1;
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
        if (proposed_.backend == "opencv" && cameras_[i].deviceIndex == proposed_.deviceIndex &&
            cameras_[i].captureApi == proposed_.captureApi) {
            row = static_cast<int>(i);
        }
    }
    cameraList_->setCurrentRow(row);

    const QString api = QString::fromStdString(proposed_.captureApi);
    if (api_->findData(api) < 0) api_->addItem(apiLabel(proposed_.captureApi), api);
    api_->setCurrentIndex(api_->findData(api));

    const QSize size(proposed_.width > 0 ? proposed_.width : 0, proposed_.height > 0 ? proposed_.height : 0);
    const QString format = QString::fromStdString(proposed_.pixelFormat);
    int mode = -1;
    for (int i = 0; i < mode_->count(); ++i) {
        if (mode_->itemData(i).toSize() == size && mode_->itemData(i, kPixelFormatRole).toString() == format) mode = i;
    }
    if (mode < 0) {
        mode_->addItem(modeText(size.width(), size.height(), proposed_.pixelFormat), size);
        mode = mode_->count() - 1;
        mode_->setItemData(mode, format, kPixelFormatRole);
    }
    mode_->setCurrentIndex(mode);

    const bool fixed = proposed_.exposureMs.has_value();
    fixedExposure_->setChecked(fixed);
    autoExposure_->setChecked(!fixed);
    const double ms = fixed ? *proposed_.exposureMs : moticamA5Profile().exposureMs.value_or(7.8125);
    int step = -1;
    for (int i = 0; i < exposure_->count(); ++i) {
        if (std::abs(exposure_->itemData(i).toDouble() - ms) < 1e-9) step = i;
    }
    if (step < 0) {
        exposure_->addItem(exposureText(ms), ms);
        step = exposure_->count() - 1;
    }
    exposure_->setCurrentIndex(step);
    exposure_->setEnabled(fixed);

    const bool locked = proposed_.whiteBalanceKelvin.has_value();
    lockedColour_->setChecked(locked);
    autoColour_->setChecked(!locked);
    kelvin_->setValue(static_cast<int>(std::lround(proposed_.whiteBalanceKelvin.value_or(kA5Kelvin))));
    kelvin_->setEnabled(locked);
}

void CameraSetupDialog::refreshStatus() {
    const CameraSetupReview review = this->review();
    const QString camera = cameraText(proposed_);

    // Under the preview: what is running now.
    QString live;
    if (searching_) {
        live = tr("Recherche des caméras…");
    } else if (report_) {
        live = tr("%1 : %2").arg(camera, grantedModeText(*report_));
        if (fps_ > 0.0) live += tr(" — %1 images/s").arg(fps_, 0, 'f', 1);
    } else if (!failure_.isEmpty()) {
        live = tr("%1 : ne s'ouvre pas").arg(camera);
    } else {
        live = tr("%1 : ouverture…").arg(camera);
    }
    liveStatus_->setText(live);
    search_->setEnabled(!searching_);

    // The mode: what was asked, what the camera granted.
    if (report_) {
        QString text = tr("Demandé : %1.<br>La caméra fournit : <b>%2</b>.")
                           .arg(modeText(proposed_.width, proposed_.height, proposed_.pixelFormat),
                                grantedModeText(*report_));
        if (review.frameSizeAdopted || review.pixelFormatAdopted) {
            text += tr("<br>⚠ Pas le mode demandé : c'est celui-ci qui sera enregistré. Essayez une autre interface ou un "
                       "autre mode.");
        } else if (!report_->frameSizeApplied || !report_->pixelFormatApplied) {
            text += tr("<br>⚠ La caméra n'indique pas tout son mode : vérification impossible.");
        } else {
            text += QStringLiteral(" ✓");
        }
        granted_->setText(theme::statusHtml(text));
    } else if (!failure_.isEmpty()) {
        granted_->setText(theme::statusHtml(
            tr("⚠ La caméra ne s'ouvre pas avec ces réglages :<br>%1").arg(failure_.toHtmlEscaped())));
    } else {
        granted_->setText(tr("Ouverture de la caméra…"));
    }

    if (saved_.calibrations.empty()) {
        calibrations_->setText(tr("Aucun grossissement calibré : calibrez-en un sur la mire une fois la caméra réglée."));
    } else if (!report_) {
        calibrations_->clear();
    } else if (review.frameSizeUnknown) {
        calibrations_->setText(theme::statusHtml(tr("⚠ Taille d'image inconnue : Calibrations non vérifiables.")));
    } else if (review.calibrationsLost()) {
        QStringList lost;
        for (const Calibration& calibration : review.unusableCalibrations) {
            lost << tr("%1 (mesuré en %2×%3)")
                        .arg(QString::fromStdString(calibration.grossissement))
                        .arg(calibration.frameWidth)
                        .arg(calibration.frameHeight);
        }
        calibrations_->setText(theme::statusHtml(
            tr("⚠ En %1, ces Calibrations ne s'appliquent plus, ni celles qui en sont déduites : %2. Recalibrez un "
               "objectif sur la mire après l'enregistrement.")
                .arg(grantedModeText(*report_), lost.join(QStringLiteral(", ")))));
    } else {
        calibrations_->setText(theme::statusHtml(tr("✓ Les Calibrations du poste s'appliquent dans ce mode.")));
    }

    // Exposure and colour temperature: applied or refused, live.
    QString exposure;
    QString colour;
    if (report_) {
        if (proposed_.exposureMs) {
            exposure = report_->exposureApplied
                           ? tr("✓ Exposition de %1 appliquée.").arg(exposureText(*proposed_.exposureMs))
                           : tr("⚠ La caméra a refusé l'exposition de %1.").arg(exposureText(*proposed_.exposureMs));
        } else {
            exposure = report_->exposureApplied ? tr("✓ Exposition automatique.")
                                                : tr("⚠ La caméra a refusé l'exposition automatique : elle garde la "
                                                     "dernière exposition réglée.");
        }
        if (fps_ > 0.0) exposure += tr("<br>Cadence mesurée : %1 images/s.").arg(fps_, 0, 'f', 1);
        if (proposed_.whiteBalanceKelvin) {
            colour = report_->whiteBalanceApplied
                         ? tr("✓ Température de couleur verrouillée à %1 K.").arg(*proposed_.whiteBalanceKelvin, 0, 'f', 0)
                         : tr("⚠ La caméra a refusé %1 K : les couleurs peuvent dériver.")
                               .arg(*proposed_.whiteBalanceKelvin, 0, 'f', 0);
        } else {
            colour = report_->whiteBalanceApplied ? tr("✓ Balance des blancs automatique.")
                                                  : tr("⚠ La caméra a refusé la balance des blancs automatique.");
        }
    }
    exposureStatus_->setText(theme::statusHtml(exposure));
    colourStatus_->setText(theme::statusHtml(colour));

    // The White reference.
    const WhiteBalance& savedGains = saved_.camera.whiteReference;
    QString white;
    if (measuringWhite_) {
        white = tr("Mesure…");
    } else if (whiteMeasured_) {
        white = tr("✓ Référence blanche mesurée (%1) : elle sera enregistrée.").arg(gainsText(proposed_.whiteReference));
    } else if (review.whiteReferenceMissing) {
        white = tr("Pas encore de référence blanche sur ce microscope : mesurez-la.");
    } else if (review.whiteReferenceStale) {
        white = tr("⚠ Référence blanche (%1) mesurée avec d'autres réglages : mesurez-la à nouveau.")
                    .arg(gainsText(savedGains));
    } else {
        white = tr("✓ Référence blanche (%1) valable. Remesurez-la si un champ vide n'est pas neutre.")
                    .arg(gainsText(proposed_.whiteReference));
    }
    whiteStatus_->setText(theme::statusHtml(white));
    measureWhite_->setEnabled(report_.has_value() && !measuringWhite_);

    // What saving means.
    summary_->setText(theme::statusHtml(summaryHtml(review)));
    recalibrate_->setVisible(report_.has_value() && review.calibrationsLost());
    save_->setEnabled(report_.has_value() && !searching_ && !measuringWhite_);
}

QString CameraSetupDialog::summaryHtml(const CameraSetupReview& review) const {
    if (!report_) {
        if (failure_.isEmpty()) return tr("Ouverture de la caméra avec ces réglages…");
        return tr("⚠ La caméra ne s'ouvre pas avec ces réglages : rien ne peut être enregistré.<br>%1")
            .arg(failure_.toHtmlEscaped());
    }
    const CameraConfig& profile = review.profile;
    QStringList rows;
    rows << tr("<b>Caméra</b> : %1").arg(cameraText(profile));

    QString mode = modeText(profile.width, profile.height, profile.pixelFormat);
    if (review.frameSizeAdopted || review.pixelFormatAdopted) {
        mode += tr(" — ⚠ fourni par la caméra au lieu de %1")
                    .arg(modeText(proposed_.width, proposed_.height, proposed_.pixelFormat));
    }
    rows << tr("<b>Mode</b> : %1").arg(mode);

    QString exposure = profile.exposureMs ? exposureText(*profile.exposureMs) : tr("automatique");
    if (review.exposureRefused) exposure += tr(" — ⚠ refusée par la caméra");
    rows << tr("<b>Exposition</b> : %1").arg(exposure);

    QString colour = profile.whiteBalanceKelvin ? QStringLiteral("%1 K").arg(*profile.whiteBalanceKelvin, 0, 'f', 0)
                                                : tr("automatique");
    if (review.whiteBalanceRefused) colour += tr(" — ⚠ refusée par la caméra");
    rows << tr("<b>Température de couleur</b> : %1").arg(colour);

    QString white;
    if (whiteMeasured_) {
        white = tr("nouvelle mesure (%1)").arg(gainsText(profile.whiteReference));
    } else if (review.whiteReferenceMissing) {
        white = tr("⚠ pas encore mesurée");
    } else if (review.whiteReferenceStale) {
        white = tr("⚠ mesurée avec d'autres réglages : à remesurer");
    } else {
        white = tr("inchangée (%1)").arg(gainsText(profile.whiteReference));
    }
    rows << tr("<b>Référence blanche</b> : %1").arg(white);

    if (review.calibrationsLost()) {
        QStringList lost;
        for (const Calibration& calibration : review.unusableCalibrations) {
            lost << QString::fromStdString(calibration.grossissement);
        }
        rows << tr("<b>Calibrations</b> : ⚠ %1 et les grossissements déduits ne s'appliqueront plus : recalibrez un "
                   "objectif sur la mire.")
                    .arg(lost.join(QStringLiteral(", ")));
    }
    if (review.exposureRefused || review.whiteBalanceRefused) {
        rows << tr("<i>Un réglage refusé est redemandé à chaque ouverture, et le bandeau le signale.</i>");
    }
    rows << QString();
    rows << (review.changed ? tr("Enregistrer remplace le profil caméra du poste.")
                            : tr("Rien n'a changé : c'est déjà le profil caméra du poste."));
    return rows.join(QStringLiteral("<br>"));
}

} // namespace msapp
