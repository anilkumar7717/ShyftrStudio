#include "MediaBinWidget.h"
#include "../media/MediaCache.h"
#include "Theme.h"
#include "Widgets.h"

#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ss {

static const char *kMediaMime = "application/x-shyftr-media";

QStringList MediaListWidget::mimeTypes() const
{
    return {kMediaMime};
}

QMimeData *MediaListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{
    auto *md = new QMimeData;
    QStringList ids;
    for (auto *it : items)
        ids << it->data(Qt::UserRole).toString();
    md->setData(kMediaMime, ids.join('\n').toUtf8());
    return md;
}

void MediaListWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragEnterEvent(e);
}

void MediaListWidget::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragMoveEvent(e);
}

void MediaListWidget::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths);
        e->acceptProposedAction();
        return;
    }
    e->ignore();
}

MediaBinWidget::MediaBinWidget(Document *doc, MediaCache *cache, QWidget *parent)
    : QWidget(parent), m_doc(doc), m_cache(cache)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    auto *imp = new QPushButton("Import…");
    imp->setToolTip("Import video, audio or images (⌘I)");
    auto *rec = new QPushButton("Record");
    rec->setToolTip("Record a voice-over from a microphone");
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Search media");
    m_filter->setClearButtonEnabled(true);
    v->addWidget(flowRow({imp, rec}));
    v->addWidget(m_filter);
    m_list = new MediaListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(128, 72));
    m_list->setGridSize(QSize(146, 112));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_list->setAcceptDrops(true);
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setAccessibleName("Media bin");
    v->addWidget(m_list, 1);
    auto *hint = new QLabel("Drag media to the timeline, or double-click to add at the playhead.");
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    v->addWidget(hint);

    connect(imp, &QPushButton::clicked, this, &MediaBinWidget::importRequested);
    connect(rec, &QPushButton::clicked, this, &MediaBinWidget::recordRequested);
    connect(m_filter, &QLineEdit::textChanged, this, &MediaBinWidget::refresh);
    connect(m_list, &MediaListWidget::filesDropped, this, &MediaBinWidget::filesDropped);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        emit addToTimelineRequested({it->data(Qt::UserRole).toString()});
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, &MediaBinWidget::showMenu);
    connect(doc, &Document::projectChanged, this, &MediaBinWidget::refresh);
    connect(doc, &Document::projectReset, this, &MediaBinWidget::refresh);
    connect(cache, &MediaCache::updated, this, &MediaBinWidget::refreshIcons);
    refresh();
}

QStringList MediaBinWidget::selectedMediaIds() const
{
    QStringList ids;
    for (auto *it : m_list->selectedItems())
        ids << it->data(Qt::UserRole).toString();
    return ids;
}

static QIcon iconFor(const MediaAsset &m, const QImage &thumb)
{
    QPixmap pm(128, 72);
    pm.fill(QColor("#22252C"));
    QPainter p(&pm);
    if (!thumb.isNull()) {
        const QImage s = thumb.scaled(128, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage((128 - s.width()) / 2, (72 - s.height()) / 2, s);
    } else {
        p.setPen(Theme::textDim());
        p.drawText(pm.rect(), Qt::AlignCenter, m.kind == MediaKind::Audio ? "♪ Audio" : m.kind == MediaKind::Image ? "Image" : "Video");
    }
    if (m.kind != MediaKind::Image) {
        const QString d = formatClock(m.duration, false).mid(3);
        p.fillRect(QRect(128 - 44, 72 - 16, 44, 16), QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(8);
        p.setFont(f);
        p.drawText(QRect(128 - 44, 72 - 16, 42, 16), Qt::AlignRight | Qt::AlignVCenter, d);
    }
    if (m.missing) {
        p.fillRect(pm.rect(), QColor(229, 72, 77, 110));
        p.setPen(Qt::white);
        p.drawText(pm.rect(), Qt::AlignCenter, "OFFLINE");
    }
    p.end();
    return QIcon(pm);
}

void MediaBinWidget::refresh()
{
    const QStringList sel = selectedMediaIds();
    m_list->clear();
    if (!m_doc->isOpen())
        return;
    m_cache->setProjectDir(m_doc->projectDir());
    const QString filter = m_filter->text().trimmed();
    for (const MediaAsset &m : m_doc->project().media) {
        if (!filter.isEmpty() && !m.name.contains(filter, Qt::CaseInsensitive))
            continue;
        auto *it = new QListWidgetItem(iconFor(m, m_cache->thumbnail(m)), m.name);
        it->setData(Qt::UserRole, m.id);
        QString tip = QString("%1\n%2").arg(m.name, m.path);
        if (m.kind != MediaKind::Audio)
            tip += QString("\n%1×%2").arg(m.width).arg(m.height);
        if (m.kind == MediaKind::Video)
            tip += QString(" · %1 fps%2").arg(m.fps.toDouble(), 0, 'f', 2).arg(m.variableFrameRate ? " (variable)" : "");
        if (m.kind != MediaKind::Image)
            tip += "\nDuration " + formatClock(m.duration);
        if (!m.videoCodec.isEmpty() || !m.audioCodec.isEmpty())
            tip += "\nCodecs " + QStringList{m.videoCodec, m.audioCodec}.join(' ').trimmed();
        if (m.missing)
            tip += "\n⚠ File not found — use File → Relink Media";
        it->setToolTip(tip);
        m_list->addItem(it);
        if (sel.contains(m.id))
            it->setSelected(true);
    }
}

void MediaBinWidget::refreshIcons(const QString &mediaId)
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() != mediaId)
            continue;
        if (const MediaAsset *m = m_doc->project().findMedia(mediaId))
            it->setIcon(iconFor(*m, m_cache->thumbnail(*m)));
    }
}

void MediaBinWidget::showMenu(const QPoint &pos)
{
    const QStringList ids = selectedMediaIds();
    QMenu menu(this);
    if (!ids.isEmpty()) {
        menu.addAction("Add to timeline at playhead", [this, ids]() { emit addToTimelineRequested(ids); });
        if (ids.size() == 1)
            if (const MediaAsset *m = m_doc->project().findMedia(ids.first()))
                if (!m->missing) {
                    const QString dir = QFileInfo(m->path).absolutePath();
                    menu.addAction("Show in folder", [dir]() { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });
                }
        menu.addAction("Remove from project", [this, ids]() {
            QStringList used;
            for (const auto &t : m_doc->project().tracks)
                for (const auto &c : t.clips)
                    if (ids.contains(c.mediaId))
                        used << c.mediaId;
            if (!used.isEmpty() &&
                QMessageBox::question(this, "Remove media",
                                      "Some of this media is used on the timeline. Remove those clips too?") != QMessageBox::Yes)
                return;
            m_doc->apply("Remove media", [&](Project &p) {
                QStringList clipIds;
                for (const auto &t : p.tracks)
                    for (const auto &c : t.clips)
                        if (ids.contains(c.mediaId))
                            clipIds << c.id;
                for (auto &t : p.tracks)
                    for (int i = t.clips.size() - 1; i >= 0; --i)
                        if (clipIds.contains(t.clips[i].id))
                            t.clips.removeAt(i);
                // transcripts bound to removed media and their caption tracks
                QStringList trIds;
                for (int i = p.transcripts.size() - 1; i >= 0; --i)
                    if (ids.contains(p.transcripts[i].mediaId)) {
                        trIds << p.transcripts[i].id;
                        p.transcripts.removeAt(i);
                    }
                for (int i = p.tracks.size() - 1; i >= 0; --i)
                    if (p.tracks[i].kind == TrackKind::Caption && trIds.contains(p.tracks[i].transcriptId))
                        p.tracks.removeAt(i);
                for (int i = p.media.size() - 1; i >= 0; --i)
                    if (ids.contains(p.media[i].id))
                        p.media.removeAt(i);
                for (auto &t : p.tracks)
                    for (auto &c : t.clips)
                        if (!c.linkId.isEmpty() && p.linkedClips(c.id).size() < 2)
                            c.linkId.clear();
                return true;
            });
        });
        menu.addSeparator();
    }
    menu.addAction("Import media…", this, &MediaBinWidget::importRequested);
    menu.addAction("Relink missing media…", this, &MediaBinWidget::relinkRequested);
    menu.exec(m_list->mapToGlobal(pos));
}

} // namespace ss
