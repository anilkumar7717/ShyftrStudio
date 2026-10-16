#pragma once

#include <QIcon>
#include <QListWidget>
#include <QString>

namespace ss {

// Shared UI for the professional text styles (thumbnails rendered with the real renderer).
namespace TextStyleUi {
QIcon thumbnail(const QString &styleId, const QSize &size);
// A wrapping grid of style thumbnails; item data(Qt::UserRole) is the style id.
QListWidget *makeGrid(QWidget *parent = nullptr, const QSize &thumb = QSize(96, 54));
// First family of a "Futura, Century Gothic" style font list that is installed (for font pickers).
QString installedFamily(const QString &familyList);
} // namespace TextStyleUi

} // namespace ss
