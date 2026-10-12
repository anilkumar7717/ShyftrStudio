#pragma once

#include <QColor>
#include <QIcon>

class QApplication;

namespace ss {

// Dark editing theme with an optional high-contrast variant (accessibility).
namespace Theme {
void apply(QApplication &app, bool highContrast, double uiScale);
QIcon appIcon();
QColor accent();
QColor clipVideo();
QColor clipAudio();
QColor clipImage();
QColor clipTitle();
QColor clipShape();
QColor clipCaption();
QColor panel();
QColor panelAlt();
QColor text();
QColor textDim();
QColor danger();
bool isHighContrast();
} // namespace Theme

} // namespace ss
