#include "theme.hpp"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <utility>
#include <vector>

namespace msapp::theme {

namespace {

// Translucent tones, for the QSS only.
const QColor accentSoft(0xc8, 0x79, 0x4a, 0x33);    // selection, checked
const QColor accentBorder(0xc8, 0x79, 0x4a, 0x80);
const QColor hoverSoft(0xff, 0xff, 0xff, 0x0a);
const QColor pressed(0x26, 0x26, 0x28);
const QColor accentHover(0xd6, 0x8a, 0x5c);
const QColor accentPressed(0xb2, 0x68, 0x3c);
const QColor onAccent(0x1c, 0x11, 0x08);
const QColor accentBg(0x2e, 0x22, 0x19);
const QColor warningBg(0x2b, 0x24, 0x14);
const QColor warningBorder(0x54, 0x45, 0x1f);
const QColor dangerBg(0x2e, 0x1b, 0x1a);
const QColor dangerBorder(0x5a, 0x2c, 0x29);
const QColor scrollHandle(0x3d, 0x3d, 0x41);
const QColor scrollHandleHover(0x52, 0x52, 0x57);

// @name@ is replaced by the colour of that name.
const char* const kStyleSheet = R"(
QMainWindow, QDialog { background: @window@; }
QMainWindow::separator { background: @border@; width: 1px; height: 1px; }
QToolTip { background: @raised@; color: @text@; border: 1px solid @borderStrong@; padding: 6px 8px; }

QMenuBar { background: @window@; border-bottom: 1px solid @border@; padding: 2px 6px; }
QMenuBar::item { background: transparent; padding: 6px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: @hover@; }
QMenuBar::item:pressed { background: @raised@; }
QMenu { background: @raised@; border: 1px solid @borderStrong@; padding: 4px; }
QMenu::item { padding: 7px 28px 7px 12px; border-radius: 4px; }
QMenu::item:selected { background: @accentSoft@; color: @text@; }
QMenu::item:disabled { color: @textFaint@; }
QMenu::separator { height: 1px; background: @border@; margin: 4px 8px; }

QToolBar { background: @window@; border: none; border-bottom: 1px solid @border@; padding: 6px 10px; spacing: 8px; }
QToolBar::separator { background: @border@; width: 1px; margin: 6px 6px; }
QToolBar QLabel { color: @textMuted@; }
QToolButton { background: transparent; color: @text@; border: 1px solid transparent; border-radius: 6px; padding: 6px 10px; }
QToolButton:hover { background: @hover@; }
QToolButton:pressed { background: @pressed@; }
QToolButton:checked { background: @accentSoft@; border-color: @accentBorder@; }

QStatusBar { background: @window@; border-top: 1px solid @border@; color: @textMuted@; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textMuted@; padding: 0 8px; }

QDockWidget { color: @textMuted@; }
QDockWidget::title { background: @window@; padding: 9px 14px; border-bottom: 1px solid @border@; text-align: left; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

QPushButton { background: @raised@; color: @text@; border: 1px solid @border@; border-radius: 6px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { background: @hover@; border-color: @borderStrong@; }
QPushButton:pressed { background: @pressed@; }
QPushButton:focus { border-color: @accentBorder@; }
QPushButton:checked { background: @accentSoft@; border-color: @accent@; color: @accentText@; }
QPushButton:disabled { background: @window@; border-color: @border@; color: @textFaint@; }
QPushButton[variant="primary"] { background: @accent@; border-color: @accent@; color: @onAccent@; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: @accentHover@; border-color: @accentHover@; }
QPushButton[variant="primary"]:pressed { background: @accentPressed@; border-color: @accentPressed@; }
QPushButton[variant="primary"]:disabled { background: @raised@; border-color: @border@; color: @textFaint@; }
QPushButton[variant="danger"] { color: @danger@; }
QPushButton[variant="danger"]:hover { background: @dangerBg@; border-color: @dangerBorder@; }
QPushButton[variant="danger"]:disabled { color: @textFaint@; }
QPushButton[variant="quiet"] { background: transparent; border-color: transparent; color: @textMuted@; }
QPushButton[variant="quiet"]:hover { background: @hover@; color: @text@; }
QPushButton[size="large"] { padding: 8px 22px; min-height: 22px; }
QPushButton[flagged="true"]:disabled { background: @accentSoft@; border-color: @accentBorder@; color: @accentText@; }

QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { background: @raised@; color: @text@; border: 1px solid @border@; border-radius: 6px; padding: 4px 8px; min-height: 22px; selection-background-color: @accent@; selection-color: @onAccent@; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: @borderStrong@; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color: @accent@; }
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled { background: @window@; color: @textFaint@; }
QComboBox QAbstractItemView { background: @raised@; color: @text@; border: 1px solid @borderStrong@; padding: 4px; outline: 0; selection-background-color: @accentSoft@; selection-color: @text@; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 26px; border: none; background: transparent; }
QComboBox::down-arrow { image: url(:/metallo/chevron-down.png); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url(:/metallo/chevron-down-disabled.png); }
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 22px; border: none; background: transparent; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; border: none; background: transparent; }
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: @hoverSoft@; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/metallo/chevron-up.png); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/metallo/chevron-down.png); width: 10px; height: 10px; }
QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:off { image: url(:/metallo/chevron-up-disabled.png); }
QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:off { image: url(:/metallo/chevron-down-disabled.png); }

QCheckBox, QRadioButton { spacing: 8px; color: @text@; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
QCheckBox::indicator { background: @raised@; border: 1px solid @borderStrong@; border-radius: 4px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @accent@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; image: url(:/metallo/check.png); }
QRadioButton::indicator { background: @raised@; border: 1px solid @borderStrong@; border-radius: 8px; }
QRadioButton::indicator:checked { border-color: @accent@; background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 @onAccent@, stop:0.32 @onAccent@, stop:0.4 @accent@, stop:1 @accent@); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background: @window@; border-color: @border@; }

QListWidget { background: @surface@; color: @text@; border: 1px solid @border@; border-radius: 8px; padding: 4px; outline: 0; }
QListWidget::item { padding: 6px 8px; border-radius: 4px; }
QListWidget::item:hover { background: @hoverSoft@; }
QListWidget::item:selected { background: @accentSoft@; color: @text@; }

QGroupBox { background: @surface@; border: 1px solid @border@; border-radius: 10px; margin-top: 24px; padding: 10px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 2px; padding: 0 0 6px 0; color: @textMuted@; }

QLabel[role="title"] { color: @text@; font-size: 14pt; font-weight: 600; }
QLabel[role="section"] { color: @textMuted@; font-weight: 600; }
QLabel[role="muted"] { color: @textMuted@; }
QLabel[role="faint"] { color: @textFaint@; }
QLabel[role="readout"] { color: @text@; font-size: 12pt; font-weight: 600; }
QLabel[callout="note"] { background: @raised@; border: 1px solid @border@; border-radius: 8px; padding: 10px 12px; }
QLabel[callout="accent"] { background: @accentBg@; border: 1px solid @accentBorder@; border-radius: 8px; padding: 10px 12px; color: @text@; }
QLabel[callout="warning"] { background: @warningBg@; border: 1px solid @warningBorder@; border-radius: 8px; padding: 10px 12px; color: @warning@; }
QLabel[banner="warning"] { background: @warningBg@; border-bottom: 1px solid @warningBorder@; padding: 10px 16px; color: @warning@; }
QLabel[banner="danger"] { background: @dangerBg@; border-bottom: 1px solid @dangerBorder@; padding: 10px 16px; color: @danger@; }
QLabel[chip="ok"] { background: @raised@; border: 1px solid @border@; border-radius: 12px; padding: 3px 10px; color: @textMuted@; }
QLabel[chip="warning"] { background: @warningBg@; border: 1px solid @warningBorder@; border-radius: 12px; padding: 3px 10px; color: @warning@; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle { background: @scrollHandle@; border-radius: 4px; }
QScrollBar::handle:vertical { min-height: 32px; margin: 0 2px; }
QScrollBar::handle:horizontal { min-width: 32px; margin: 2px 0; }
QScrollBar::handle:hover { background: @scrollHandleHover@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QGraphicsView { border: none; }
)";

QString styleSheet() {
    const std::vector<std::pair<const char*, QColor>> colours = {
        {"canvas", canvas},
        {"window", window},
        {"surface", surface},
        {"raised", raised},
        {"hover", hover},
        {"hoverSoft", hoverSoft},
        {"pressed", pressed},
        {"border", border},
        {"borderStrong", borderStrong},
        {"text", text},
        {"textMuted", textMuted},
        {"textFaint", textFaint},
        {"accent", accent},
        {"accentHover", accentHover},
        {"accentPressed", accentPressed},
        {"accentSoft", accentSoft},
        {"accentBorder", accentBorder},
        {"accentText", accentText},
        {"accentBg", accentBg},
        {"onAccent", onAccent},
        {"warning", warning},
        {"warningBg", warningBg},
        {"warningBorder", warningBorder},
        {"danger", danger},
        {"dangerBg", dangerBg},
        {"dangerBorder", dangerBorder},
        {"scrollHandle", scrollHandle},
        {"scrollHandleHover", scrollHandleHover},
    };
    QString sheet = QString::fromUtf8(kStyleSheet);
    for (const auto& [name, colour] : colours) {
        sheet.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), colour.name(QColor::HexArgb));
    }
    return sheet;
}

QPalette palette() {
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, raised);
    p.setColor(QPalette::ToolTipBase, raised);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, textFaint);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, raised);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, danger);
    p.setColor(QPalette::Light, QColor(0x3e, 0x3e, 0x42));
    p.setColor(QPalette::Midlight, QColor(0x33, 0x33, 0x36));
    p.setColor(QPalette::Mid, QColor(0x2a, 0x2a, 0x2d));
    p.setColor(QPalette::Dark, QColor(0x15, 0x15, 0x16));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, onAccent);
    p.setColor(QPalette::Link, accentText);
    p.setColor(QPalette::LinkVisited, accentText);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, textFaint);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, borderStrong);
    return p;
}

} // namespace

void apply(QApplication& app) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(palette());
    // 9 pt, the Windows default, is small to read at a microscope bench.
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0 && font.pointSizeF() < 10.0) font.setPointSizeF(10.0);
    QApplication::setFont(font);
    app.setStyleSheet(styleSheet());
    QApplication::setWindowIcon(appIcon());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Dark title bars on Windows, whatever the Windows setting.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
}

QIcon appIcon() {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/metallo/metallo-%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

void setRole(QWidget* widget, const char* property, const QString& value) {
    if (widget->property(property).toString() == value) return;
    widget->setProperty(property, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString statusHtml(QString html) {
    html.replace(QStringLiteral("✓"),
                 QStringLiteral("<span style=\"color:%1\">✓</span>").arg(success.name()));
    html.replace(QStringLiteral("⚠"),
                 QStringLiteral("<span style=\"color:%1\">⚠</span>").arg(warning.name()));
    return html;
}

} // namespace msapp::theme
