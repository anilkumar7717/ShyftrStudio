#include "Theme.h"
#include "../captions/CaptionStyle.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <algorithm>

namespace ss {
namespace Theme {

static bool g_hc = false;

bool isHighContrast() { return g_hc; }
QColor accent() { return g_hc ? QColor("#FFD400") : QColor("#5B8CFF"); }
QColor clipVideo() { return g_hc ? QColor("#0050FF") : QColor("#3A5BA8"); }
QColor clipAudio() { return g_hc ? QColor("#00A040") : QColor("#2E7D5B"); }
QColor clipImage() { return g_hc ? QColor("#008C8C") : QColor("#2B7A87"); }
QColor clipTitle() { return g_hc ? QColor("#B000FF") : QColor("#7A4FC4"); }
QColor clipShape() { return g_hc ? QColor("#FF7A00") : QColor("#B86B2E"); }
QColor clipCaption() { return g_hc ? QColor("#FFD400") : QColor("#C9A227"); }
QColor panel() { return g_hc ? QColor("#000000") : QColor("#1E2026"); }
QColor panelAlt() { return g_hc ? QColor("#111111") : QColor("#25282F"); }
QColor text() { return g_hc ? QColor("#FFFFFF") : QColor("#E6E8EE"); }
QColor textDim() { return g_hc ? QColor("#E0E0E0") : QColor("#9AA0AD"); }
QColor danger() { return QColor("#E5484D"); }

void apply(QApplication &app, bool highContrast, double uiScale)
{
    g_hc = highContrast;
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor window = highContrast ? QColor("#000000") : QColor("#17191E");
    const QColor base = highContrast ? QColor("#000000") : QColor("#121418");
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, panelAlt());
    p.setColor(QPalette::ToolTipBase, panelAlt());
    p.setColor(QPalette::ToolTipText, text());
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::PlaceholderText, textDim());
    p.setColor(QPalette::Button, panel());
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, highContrast ? Qt::black : Qt::white);
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#5E6370"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#5E6370"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#5E6370"));
    app.setPalette(p);

    QFont f = app.font();
    f.setPointSizeF(std::max(9.0, 12.0 * uiScale));
    app.setFont(f);

    const QString border = highContrast ? "#FFFFFF" : "#2E323B";
    const QString focus = accent().name();
    app.setStyleSheet(QString(R"(
        QToolTip { border: 1px solid %1; padding: 4px; }
        QDockWidget::title { padding: 6px 8px; background: %2; font-weight: 600; }
        QGroupBox { border: 1px solid %1; border-radius: 6px; margin-top: 14px; padding-top: 8px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %3; font-weight: 600; }
        QPushButton { padding: 5px 12px; border: 1px solid %1; border-radius: 5px; background: %2; }
        QPushButton:hover { border-color: %4; }
        QPushButton:focus { border: 2px solid %4; }
        QPushButton#primary { background: %4; color: %5; border: none; font-weight: 600; }
        QPushButton#primary:disabled { background: #3a3f4b; color: #8b909c; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
            border: 1px solid %1; border-radius: 4px; padding: 3px 5px; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 2px solid %4; }
        QTabBar::tab { padding: 6px 12px; }
        QTabBar::tab:selected { border-bottom: 2px solid %4; }
        QToolBar { spacing: 4px; padding: 3px; border: none; }
        QToolButton { padding: 4px 8px; border: 1px solid %1; border-radius: 4px; }
        QToolBar QToolButton { border: 1px solid transparent; }
        QToolButton:checked { background: %4; color: %5; border-color: %4; }
        QToolButton:hover { border-color: %4; }
        QStatusBar { color: %3; }
        QLabel#hint { color: %3; }
        QLabel#heading { font-size: 16pt; font-weight: 700; }
        QLabel#error { color: #ff6b6b; }
        QProgressBar { border: 1px solid %1; border-radius: 4px; text-align: center; height: 16px; }
        QProgressBar::chunk { background: %4; border-radius: 3px; }
    )")
                          .arg(border, panel().name(), textDim().name(), focus,
                               highContrast ? "#000000" : "#FFFFFF"));
}

QIcon appIcon()
{
    static QIcon cached;
    if (!cached.isNull())
        return cached;
    // The shipped artwork (resources/icons/app-icon-1024.png, also used for .icns/.ico).
    TemplateLibrary::instance(); // registers the engine's resources (icon lives next to the templates)
    const QImage art(":/icons/app-icon-1024.png");
    if (!art.isNull()) {
        for (int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024})
            cached.addPixmap(QPixmap::fromImage(art.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        return cached;
    }
    // Fallback mark drawn in code: rounded square with a play triangle and caption bars.
    QIcon icon;
    for (int size : {16, 32, 64, 128, 256, 512}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = size;
        QLinearGradient g(0, 0, s, s);
        g.setColorAt(0, QColor("#5B8CFF"));
        g.setColorAt(1, QColor("#7A4FC4"));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(s * 0.04, s * 0.04, s * 0.92, s * 0.92), s * 0.22, s * 0.22);
        QPainterPath tri;
        tri.moveTo(s * 0.38, s * 0.24);
        tri.lineTo(s * 0.70, s * 0.43);
        tri.lineTo(s * 0.38, s * 0.62);
        tri.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(tri);
        p.setBrush(QColor("#FFD43B"));
        p.drawRoundedRect(QRectF(s * 0.22, s * 0.70, s * 0.56, s * 0.07), s * 0.035, s * 0.035);
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawRoundedRect(QRectF(s * 0.30, s * 0.81, s * 0.40, s * 0.06), s * 0.03, s * 0.03);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace Theme
} // namespace ss
