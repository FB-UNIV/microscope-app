#pragma once

#include "qt_image.hpp"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include <filesystem>

namespace msapp {

// When a Vue was taken, in local time, from the UTC time its VueStore file
// name starts with (20261015-174353-…); invalid for any other file.
inline QDateTime vueTaken(const std::filesystem::path& vue) {
    const QString name = toQString(vue.filename());
    const QDate date = QDate::fromString(name.left(8), QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(name.mid(9, 6), QStringLiteral("HHmmss"));
    if (!date.isValid() || !time.isValid()) return {};
    return QDateTime(date, time, QTimeZone(0)).toLocalTime();
}

} // namespace msapp
