#include "fiche_pdf.hpp"

#include "fiche.hpp"
#include "qt_image.hpp"
#include "vue.hpp"

#include <QCoreApplication>
#include <QFont>
#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QRectF>

#include <exception>
#include <stdexcept>
#include <vector>

namespace msapp {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("Fiche", text);
}

// One Échantillon as it goes on the sheet.
struct Printed {
    QString name;
    QImage photo;          // null: no Vue principale, or it can't be read
    QString grossissement; // empty: uncalibrated
    bool echelle = false;  // an Échelle is drawn on the photo
    QString problem;       // why there is no photo
};

} // namespace

QStringList writeFichePdf(const VueStore& store, const std::string& department, const Tp& tp, const QString& pdfPath) {
    constexpr int kDotsPerInch = 300;
    // Larger than any cell of an A4 sheet at 300 dpi: photos are shrunk to
    // about print size, not embedded at 4K.
    const QSize printSize(2000, 2000);

    std::vector<Printed> printed;
    double aspect = 0.0;
    for (const FicheEntry& entry : ficheEntries(store, department, tp)) {
        Printed item;
        item.name = QString::fromStdString(entry.echantillon);
        if (!entry.vue) {
            item.problem = tr("Pas de Vue principale");
        } else {
            try {
                const Vue vue = Vue::load(*entry.vue);
                // The original with its Échelle only: Levels and the phase
                // map stay on screen (ADR 0022).
                const FichePhoto photo = fichePhoto(vue);
                if (aspect <= 0.0) aspect = static_cast<double>(photo.image.cols) / photo.image.rows;
                item.photo = toQImage(photo.image, printSize);
                item.echelle = photo.echelle;
                if (const auto& calibration = vue.calibration()) {
                    item.grossissement = QString::fromStdString(calibration->grossissement);
                }
            } catch (const std::exception& e) {
                item.problem = tr("Vue illisible : %1").arg(QString::fromStdString(e.what()));
            }
        }
        printed.push_back(item);
    }
    // The layout follows the photos' shape; 16:9 (the A5's 4K mode) when
    // there is no photo at all.
    const FichePage page;
    const std::vector<FicheCell> cells = layoutFiche(printed.size(), aspect > 0.0 ? aspect : 16.0 / 9.0, page);

    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Portrait);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    writer.setResolution(kDotsPerInch);
    writer.setTitle(QString::fromStdString(tp.name));
    writer.setCreator(QStringLiteral("Métallo"));

    QPainter painter;
    if (!painter.begin(&writer)) {
        throw std::runtime_error("writeFichePdf: cannot write " + pdfPath.toStdString());
    }
    const double dotsPerMm = kDotsPerInch / 25.4;
    QFont font = painter.font();
    font.setPointSizeF(10.0);
    painter.setFont(font);

    QStringList notes;
    int currentPage = 0;
    for (std::size_t i = 0; i < printed.size(); ++i) {
        const FicheCell& cell = cells[i];
        const Printed& item = printed[i];
        if (cell.page != currentPage) {
            writer.newPage();
            currentPage = cell.page;
        }
        const QRectF box(cell.x * dotsPerMm, cell.y * dotsPerMm, cell.width * dotsPerMm, cell.imageHeight * dotsPerMm);
        const QRectF caption(box.left(), box.bottom(), box.width(), page.captionHeight * dotsPerMm);

        QString captionText = item.name;
        if (!item.photo.isNull()) {
            QRectF target(QPointF(0, 0), QSizeF(item.photo.size()).scaled(box.size(), Qt::KeepAspectRatio));
            target.moveCenter(box.center());
            painter.drawImage(target, item.photo);
            if (item.grossissement.isEmpty()) {
                captionText += tr(" — Grossissement inconnu (Vue non calibrée)");
                notes << tr("%1 : Vue principale non calibrée, sans Grossissement ni Échelle").arg(item.name);
            } else {
                captionText += tr(" — Grossissement %1").arg(item.grossissement);
                // Not drawn again from the Calibration: the Fiche prints what
                // the Vue carries (ADR 0022).
                if (!item.echelle) notes << tr("%1 : Vue principale sans Échelle (retirée de la Vue)").arg(item.name);
            }
        } else {
            painter.setPen(QPen(Qt::gray, 3));
            painter.drawRect(box);
            painter.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, item.problem);
            notes << tr("%1 : %2").arg(item.name, item.problem);
        }
        painter.setPen(Qt::black);
        painter.drawText(caption, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, captionText);
    }
    painter.end();
    return notes;
}

} // namespace msapp
