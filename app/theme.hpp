#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace msapp::theme {

// Métallo's look (ADR 0023): neutral greys, so the window around a photo
// never tints how its colours look, and one copper accent, kept apart from
// the red, yellow and cyan of the marks drawn on photos. The widgets that
// paint themselves (preview, image view, histogram, Vue list) take their
// colours from here too.
inline const QColor canvas{0x11, 0x11, 0x12};   // around a photo
inline const QColor window{0x1b, 0x1b, 0x1d};
inline const QColor surface{0x23, 0x23, 0x25};  // lists, cards
inline const QColor raised{0x2c, 0x2c, 0x2f};   // buttons, fields
inline const QColor hover{0x35, 0x35, 0x38};
inline const QColor border{0x36, 0x36, 0x39};
inline const QColor borderStrong{0x48, 0x48, 0x4c};
inline const QColor text{0xec, 0xec, 0xed};
inline const QColor textMuted{0xa2, 0xa2, 0xa7};
inline const QColor textFaint{0x6e, 0x6e, 0x73};
inline const QColor accent{0xc8, 0x79, 0x4a};      // copper
inline const QColor accentText{0xe9, 0xa6, 0x7c};  // copper text on the greys
inline const QColor success{0x74, 0xc4, 0x8d};
inline const QColor warning{0xe8, 0xb8, 0x4a};
inline const QColor danger{0xf0, 0x85, 0x7c};

// Fusion with the palette and style sheet above, the app icon, a larger
// base font for reading at the bench, and dark title bars where Qt can ask
// for them (6.8 and later).
void apply(QApplication& app);

// The Métallo icon, every size it is drawn at.
QIcon appIcon();

// Sets a style sheet property, e.g. setRole(button, "variant", "primary"),
// and restyles the widget: Qt reads properties only when it polishes.
void setRole(QWidget* widget, const char* property, const QString& value);

// Rich text with its ✓ and ⚠ in the success and warning colours.
QString statusHtml(QString html);

} // namespace msapp::theme
