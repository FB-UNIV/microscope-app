#pragma once

#include <QApplication>

namespace msapp {

// A busy cursor for a slow step on the GUI thread (phase analysis, a PDF, a
// 4K export), restored however the step ends.
class WaitCursor {
public:
    WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~WaitCursor() { QApplication::restoreOverrideCursor(); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;
};

} // namespace msapp
