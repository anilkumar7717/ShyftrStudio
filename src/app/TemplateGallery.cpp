#include "TemplateGallery.h"
#include "../captions/OverlayRenderer.h"

#include <QPainter>

namespace ss {

TemplateGallery::TemplateGallery(QWidget *parent) : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSpacing(4);
    setWordWrap(true);
    setUniformItemSizes(true);
    setAccessibleName("Caption templates");
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { emit templateChosen(it->data(Qt::UserRole).toString()); });
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) { emit templateChosen(it->data(Qt::UserRole).toString()); });
    reload();
}

void TemplateGallery::setCompact(bool compact)
{
    m_compact = compact;
    reload();
}

void TemplateGallery::setAspect(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    const QSize a(w, h);
    if (a == m_aspect)
        return;
    m_aspect = a;
    reload();
}

void TemplateGallery::setCurrentTemplate(const QString &id)
{
    m_current = id;
    for (int i = 0; i < count(); ++i)
        if (item(i)->data(Qt::UserRole).toString() == id) {
            blockSignals(true);
            setCurrentRow(i);
            blockSignals(false);
        }
}

void TemplateGallery::reload()
{
    clear();
    const int base = m_compact ? 150 : 168;
    QSize thumb = m_aspect.scaled(base, base, Qt::KeepAspectRatio);
    if (thumb.height() < 70)
        thumb = m_aspect.scaled(base * 2, 70, Qt::KeepAspectRatio);
    setIconSize(thumb);
    setGridSize(QSize(thumb.width() + 16, thumb.height() + 36));

    // Render at a realistic frame size, then downscale (keeps proportions identical to export).
    const QSize frame = m_aspect.scaled(960, 960, Qt::KeepAspectRatio);
    CueInstance cue;
    cue.start = 0;
    cue.end = 3 * kUsPerSec;
    cue.speaker = 0;
    const QStringList words{"Your", "words", "appear", "here"};
    for (int i = 0; i < words.size(); ++i)
        cue.words << WordInstance{i, words[i], i * 500 * kUsPerMs, (i + 1) * 500 * kUsPerMs};
    const TimeUs t = 750 * kUsPerMs; // second word active, karaoke half-filled
    CaptionRenderer::SpeakerInfo spk{"Host", QColor("#FFD43B")};

    for (const CaptionStyle &s : TemplateLibrary::instance().all()) {
        QImage img(frame, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        QLinearGradient g(0, 0, frame.width(), frame.height());
        g.setColorAt(0, QColor("#3b4a6b"));
        g.setColorAt(0.5, QColor("#5a4e6e"));
        g.setColorAt(1, QColor("#2c5d63"));
        p.fillRect(img.rect(), g);
        CaptionStyle st = s;
        st.entry = "none"; // static thumbnail
        CaptionRenderer::paintCue(p, frame, st, cue, t, &spk);
        p.end();
        // Zoom onto the caption so the style is readable at thumbnail size.
        QRectF focus = CaptionRenderer::cueRect(frame, st, cue).adjusted(-40, -70, 40, 40);
        const double ar = double(thumb.width()) / thumb.height();
        if (focus.width() / focus.height() < ar)
            focus.setWidth(focus.height() * ar);
        else
            focus.setHeight(focus.width() / ar);
        focus.moveCenter(CaptionRenderer::cueRect(frame, st, cue).center());
        focus = focus.intersected(QRectF(img.rect()));
        const QImage crop = img.copy(focus.toAlignedRect());
        auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(crop.scaled(thumb, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                                                                    .copy(QRect(QPoint(0, 0), thumb)))),
                                       s.name);
        it->setData(Qt::UserRole, s.id);
        it->setToolTip(QString("%1\n%2\nFamily: %3").arg(s.name, s.description, s.family));
        addItem(it);
    }
    setCurrentTemplate(m_current);
}

} // namespace ss
