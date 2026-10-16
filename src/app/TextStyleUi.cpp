#include "TextStyleUi.h"
#include "../captions/OverlayRenderer.h"
#include "../captions/TextStyles.h"

#include <QFontDatabase>
#include <QHash>
#include <QPainter>

namespace ss {
namespace TextStyleUi {

QIcon thumbnail(const QString &styleId, const QSize &size)
{
    static QHash<QString, QIcon> cache;
    const QString key = styleId + QString("@%1x%2").arg(size.width()).arg(size.height());
    if (cache.contains(key))
        return cache.value(key);
    const int sc = 2; // render at 2x for crisp thumbnails on HiDPI screens
    QImage img(size * sc, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&img);
        QLinearGradient bg(0, 0, 0, img.height());
        bg.setColorAt(0, QColor("#3A3F4B"));
        bg.setColorAt(1, QColor("#22252C"));
        p.fillRect(img.rect(), bg);
        Clip c;
        c.type = ClipType::Title;
        c.out = 4 * kUsPerSec;
        c.title.text = "Title";
        c.title.sizePct = 0.36;
        c.title.animation = "none";
        TextStyles::apply(c.title, styleId);
        if (c.title.textCase == "upper")
            c.title.sizePct = 0.3;
        GraphicsRenderer::paintTitle(p, img.size(), c, kUsPerSec);
    }
    img.setDevicePixelRatio(sc);
    const QIcon icon(QPixmap::fromImage(img));
    cache.insert(key, icon);
    return icon;
}

QListWidget *makeGrid(QWidget *parent, const QSize &thumb)
{
    auto *list = new QListWidget(parent);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWrapping(true);
    list->setIconSize(thumb);
    list->setGridSize(QSize(thumb.width() + 10, thumb.height() + 26));
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSpacing(2);
    for (const TextStyle &s : TextStyles::all()) {
        auto *it = new QListWidgetItem(thumbnail(s.id, thumb), s.label);
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(s.category + " · " + s.label);
        it->setSizeHint(QSize(thumb.width() + 8, thumb.height() + 24));
        list->addItem(it);
    }
    return list;
}

QString installedFamily(const QString &familyList)
{
    const QStringList installed = QFontDatabase::families();
    for (const QString &f : familyList.split(',', Qt::SkipEmptyParts))
        if (installed.contains(f.trimmed(), Qt::CaseInsensitive))
            return f.trimmed();
    return familyList.section(',', 0, 0).trimmed();
}

} // namespace TextStyleUi
} // namespace ss
