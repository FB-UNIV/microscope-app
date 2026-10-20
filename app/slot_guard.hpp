#pragma once

#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QWidget>

#include <exception>
#include <utility>

namespace msapp {

// The boundary a Qt slot needs (review H3): an exception that leaves a slot
// unwinds into Qt's event loop and ends in std::terminate — mid-session,
// with the camera open. Runs `body` and shows what it threw under `title`
// instead. For slot paths with no catch of their own; a slot that reports
// its own errors, in its own words, keeps doing so. GUI thread only.
template <typename Body>
void guardSlot(QWidget* parent, const QString& title, Body&& body) {
    try {
        std::forward<Body>(body)();
    } catch (const std::exception& e) {
        QMessageBox::warning(parent, title, QString::fromStdString(e.what()));
    } catch (...) {
        QMessageBox::warning(parent, title, QObject::tr("Erreur inattendue."));
    }
}

} // namespace msapp
