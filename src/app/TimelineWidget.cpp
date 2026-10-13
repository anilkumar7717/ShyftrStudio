#include "TimelineWidget.h"
#include "../media/ImageFx.h"
#include "../captions/CaptionStyle.h"
#include "../core/TimelineOps.h"
#include "../media/MediaCache.h"
#include "Theme.h"
#include "EffectsBrowser.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDragLeaveEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QFileInfo>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QResizeEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ss {

using namespace TimelineOps;

static const char *kMediaMime = "application/x-shyftr-media";

TimelineWidget::TimelineWidget(Document *doc, MediaCache *cache, QWidget *parent)
    : QAbstractScrollArea(parent), m_doc(doc), m_cache(cache)
{
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("Timeline");
    setMinimumHeight(150);
    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(30);
    connect(m_autoScroll, &QTimer::timeout, this, &TimelineWidget::autoScrollStep);
    connect(doc, &Document::projectChanged, this, &TimelineWidget::rebuild);
    connect(doc, &Document::projectReset, this, &TimelineWidget::rebuild);
    connect(doc, &Document::selectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(doc, &Document::cueSelectionChanged, viewport(), qOverload<>(&QWidget::update));
    connect(doc, &Document::playheadChanged, viewport(), qOverload<>(&QWidget::update));
    connect(cache, &MediaCache::updated, viewport(), qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    rebuild();
}

void TimelineWidget::setCompact(bool c)
{
    m_compact = c;
    rebuild();
}

void TimelineWidget::rebuild()
{
    m_rows.clear();
    m_cues.clear();
    if (!m_doc->isOpen()) {
        viewport()->update();
        return;
    }
    const Project &p = m_doc->project();
    int y = topLaneHeight();
    auto add = [&](int idx, int h) {
        m_rows << Row{idx, y, h};
        y += h + 2;
    };
    // Pick the largest row density that fits the visible height (responsive on small screens).
    int nv = 0, na = 0, nc = 0;
    for (const auto &t : p.tracks)
        (t.kind == TrackKind::Video ? nv : t.kind == TrackKind::Audio ? na : nc)++;
    const int avail = viewport()->height() - rulerHeight() - 4 - topLaneHeight() - bottomLaneHeight() / 2;
    static const int heights[3][3] = {{64, 54, 38}, {46, 38, 30}, {34, 30, 24}};
    m_density = m_compact ? 1 : 0;
    while (m_density < 2) {
        const int need = nv * (heights[m_density][0] + 2) + na * (heights[m_density][1] + 2) + nc * (heights[m_density][2] + 2);
        if (need <= avail)
            break;
        ++m_density;
    }
    const int vh = heights[m_density][0], ah = heights[m_density][1], ch = heights[m_density][2];
    for (int i = p.tracks.size() - 1; i >= 0; --i)
        if (p.tracks[i].kind == TrackKind::Video)
            add(i, vh);
    for (int i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].kind == TrackKind::Caption) {
            add(i, ch);
            m_cues.insert(p.tracks[i].id, CaptionMapper::map(p, p.tracks[i]));
        }
    for (int i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].kind == TrackKind::Audio)
            add(i, ah);
    updateScrollbars();
    viewport()->update();
}

void TimelineWidget::updateScrollbars()
{
    const TimeUs dur = m_doc->isOpen() ? m_doc->project().duration() : 0;
    const double contentW = (dur / 1e6 + 30) * m_pxPerSec;
    const int visW = viewport()->width() - headerWidth();
    horizontalScrollBar()->setRange(0, std::max(0, int(contentW) - visW));
    horizontalScrollBar()->setPageStep(std::max(1, visW));
    horizontalScrollBar()->setSingleStep(20);
    const int contentH = m_rows.isEmpty() ? 0 : m_rows.last().y + m_rows.last().h + 2 + bottomLaneHeight();
    const int visH = viewport()->height() - rulerHeight();
    verticalScrollBar()->setRange(0, std::max(0, contentH - visH));
    verticalScrollBar()->setPageStep(std::max(1, visH));
}

void TimelineWidget::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    if (e->size().height() != e->oldSize().height())
        rebuild();
    else
        updateScrollbars();
}

double TimelineWidget::xForTime(TimeUs t) const
{
    return headerWidth() + t / 1e6 * m_pxPerSec - horizontalScrollBar()->value();
}

TimeUs TimelineWidget::timeForX(double x) const
{
    return TimeUs((x - headerWidth() + horizontalScrollBar()->value()) / m_pxPerSec * 1e6);
}

int TimelineWidget::rowAtY(int y) const
{
    const int cy = y - rulerHeight() + verticalScrollBar()->value();
    for (int i = 0; i < m_rows.size(); ++i)
        if (cy >= m_rows[i].y && cy < m_rows[i].y + m_rows[i].h + 2)
            return i;
    return -1;
}

int TimelineWidget::laneAtY(int y) const
{
    if (y < rulerHeight())
        return 0;
    if (m_rows.isEmpty())
        return -1;
    const int cy = y - rulerHeight() + verticalScrollBar()->value();
    if (cy < m_rows.first().y)
        return 1;
    if (cy >= m_rows.last().y + m_rows.last().h + 2)
        return -1;
    return 0;
}

QRect TimelineWidget::laneRect(int lane) const
{
    const int vs = verticalScrollBar()->value();
    if (lane > 0)
        return QRect(headerWidth(), rulerHeight() - vs, viewport()->width() - headerWidth(), topLaneHeight() - 2);
    const int top = rulerHeight() + (m_rows.isEmpty() ? topLaneHeight() : m_rows.last().y + m_rows.last().h + 2) - vs;
    return QRect(headerWidth(), top, viewport()->width() - headerWidth(), std::max(bottomLaneHeight() - 4, viewport()->height() - top - 2));
}

QRect TimelineWidget::clipRect(const Row &row, const Clip &c) const
{
    const int top = rulerHeight() + row.y - verticalScrollBar()->value();
    const double x0 = xForTime(c.start), x1 = xForTime(c.end());
    return QRect(QPoint(int(x0), top + 2), QPoint(int(std::max(x0 + 2, x1)) - 1, top + row.h - 2));
}

QColor TimelineWidget::colorFor(const Clip &c) const
{
    if (c.type == ClipType::Effect)
        return QColor("#B0458C");
    if (c.type == ClipType::Title)
        return Theme::clipTitle();
    if (c.type == ClipType::Shape)
        return Theme::clipShape();
    const MediaAsset *m = m_doc->project().findMedia(c.mediaId);
    const Track *t = nullptr;
    m_doc->project().findClip(c.id, &t);
    if (t && t->kind == TrackKind::Audio)
        return Theme::clipAudio();
    if (m && m->kind == MediaKind::Image)
        return Theme::clipImage();
    return Theme::clipVideo();
}

QStringList TimelineWidget::headerButtons(const Track &t) const
{
    switch (t.kind) {
    case TrackKind::Video: return {"H", "L"};
    case TrackKind::Audio: return {"M", "L"};
    case TrackKind::Caption: return {"CC", "H"};
    }
    return {};
}

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint &pos) const
{
    Hit h;
    if (pos.y() < rulerHeight())
        return h;
    h.row = rowAtY(pos.y());
    if (h.row < 0)
        return h;
    const Row &row = m_rows[h.row];
    const Project &p = m_doc->project();
    const Track &t = p.tracks[row.trackIndex];
    const int top = rulerHeight() + row.y - verticalScrollBar()->value();
    if (pos.x() < headerWidth()) {
        h.header = true;
        const QStringList btns = headerButtons(t);
        for (int i = 0; i < btns.size(); ++i) {
            const QRect br(headerWidth() - 8 - (btns.size() - i) * 28, top + row.h / 2 - 10, 24, 20);
            if (br.contains(pos))
                h.headerButton = btns[i];
        }
        return h;
    }
    if (t.kind == TrackKind::Caption) {
        const auto &cues = m_cues.value(t.id);
        for (int i = 0; i < cues.size(); ++i) {
            const double x0 = xForTime(cues[i].start), x1 = xForTime(cues[i].end);
            if (pos.x() >= x0 - 4 && pos.x() <= x1 + 4) {
                h.cueIndex = cues[i].cueIndex;
                h.cueEdgeStart = std::abs(pos.x() - x0) <= 5;
                h.cueEdgeEnd = !h.cueEdgeStart && std::abs(pos.x() - x1) <= 5;
                if (pos.x() >= x0 || h.cueEdgeStart)
                    return h;
            }
        }
        h.cueIndex = -1;
        return h;
    }
    for (const Clip &c : t.clips) {
        const QRect r = clipRect(row, c);
        if (!r.adjusted(-4, 0, 4, 0).contains(pos))
            continue;
        h.clipId = c.id;
        const bool isAudioTrack = t.kind == TrackKind::Audio;
        const TimeUs fin = isAudioTrack ? c.audio.fadeIn : c.fadeIn;
        const TimeUs fout = isAudioTrack ? c.audio.fadeOut : c.fadeOut;
        const QPoint fi(int(r.left() + fin / 1e6 * m_pxPerSec), r.top() + 5);
        const QPoint fo(int(r.right() - fout / 1e6 * m_pxPerSec), r.top() + 5);
        if (r.width() > 30 && (pos - fi).manhattanLength() <= 7)
            h.part = Part::FadeIn;
        else if (r.width() > 30 && (pos - fo).manhattanLength() <= 7)
            h.part = Part::FadeOut;
        else if (std::abs(pos.x() - r.left()) <= 6)
            h.part = Part::EdgeStart;
        else if (std::abs(pos.x() - r.right()) <= 6)
            h.part = Part::EdgeEnd;
        else
            h.part = Part::Body;
        return h;
    }
    return h;
}

// ------------------------------------------------------------------ painting

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), QColor("#121418"));
    if (!m_doc->isOpen())
        return;
    p.setRenderHint(QPainter::Antialiasing);
    const Project &pr = m_doc->project();

    // in/out shading
    if (pr.inPoint >= 0 && pr.outPoint > pr.inPoint) {
        const double x0 = xForTime(pr.inPoint), x1 = xForTime(pr.outPoint);
        p.fillRect(QRectF(x0, 0, x1 - x0, viewport()->height()), QColor(91, 140, 255, 28));
    }
    for (const Row &row : m_rows)
        paintRow(p, row);
    paintRuler(p);

    // playhead
    const double px = xForTime(m_doc->playhead());
    if (px >= headerWidth()) {
        p.setPen(QPen(QColor("#FF4D5E"), 1.5));
        p.drawLine(QPointF(px, 0), QPointF(px, viewport()->height()));
        QPainterPath tri;
        tri.moveTo(px - 6, 0);
        tri.lineTo(px + 6, 0);
        tri.lineTo(px, 9);
        tri.closeSubpath();
        p.fillPath(tri, QColor("#FF4D5E"));
    }
    if (m_snapLine >= 0) {
        const double sx = xForTime(m_snapLine);
        p.setPen(QPen(QColor("#FFD43B"), 1, Qt::DashLine));
        p.drawLine(QPointF(sx, rulerHeight()), QPointF(sx, viewport()->height()));
    }
    paintDropFeedback(p);
    // header column border
    p.setPen(QColor("#2E323B"));
    p.drawLine(headerWidth(), 0, headerWidth(), viewport()->height());
}

void TimelineWidget::paintLaneHint(QPainter &p, int lane, bool active, const QString &text)
{
    QRect r = laneRect(lane).adjusted(4, 2, -4, -2);
    if (lane < 0)
        r.setHeight(std::min(r.height(), bottomLaneHeight() - 6));
    if (r.height() < 6)
        return;
    QColor c = Theme::accent();
    c.setAlpha(active ? 60 : 18);
    p.setBrush(c);
    QColor pen = Theme::accent();
    pen.setAlpha(active ? 255 : 110);
    p.setPen(QPen(pen, 1, Qt::DashLine));
    p.drawRoundedRect(r, 5, 5);
    if (r.height() >= 14) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        p.setPen(active ? Qt::white : Theme::textDim());
        p.drawText(r.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

void TimelineWidget::paintDropFeedback(QPainter &p)
{
    const Project &pr = m_doc->project();
    const int vs = verticalScrollBar()->value();
    auto rowTop = [&](const Row &row) { return rulerHeight() + row.y - vs; };
    auto rowOfTrack = [&](const QString &id) -> int {
        for (int i = 0; i < m_rows.size(); ++i)
            if (pr.tracks[m_rows[i].trackIndex].id == id)
                return i;
        return -1;
    };
    auto insertMarker = [&](int ri, TimeUs at) {
        if (ri < 0 || at < 0)
            return;
        const Row &row = m_rows[ri];
        const double x = std::max<double>(xForTime(at), headerWidth() + 3);
        const int top = rowTop(row);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#FFD43B"));
        p.drawRect(QRectF(x - 1.5, top - 2, 3, row.h + 4));
        QPainterPath l, r;
        l.moveTo(x - 3, top + row.h / 2.0);
        l.lineTo(x - 9, top + row.h / 2.0 - 5);
        l.lineTo(x - 9, top + row.h / 2.0 + 5);
        r.moveTo(x + 3, top + row.h / 2.0);
        r.lineTo(x + 9, top + row.h / 2.0 - 5);
        r.lineTo(x + 9, top + row.h / 2.0 + 5);
        p.fillPath(l, QColor("#FFD43B"));
        p.fillPath(r, QColor("#FFD43B"));
    };
    auto ghostRect = [&](const QRect &lane, TimeUs start, TimeUs dur, const QString &label, const QColor &col) {
        const double x0 = xForTime(start), x1 = xForTime(start + dur);
        QRectF r(x0, lane.top() + 1, std::max(6.0, x1 - x0), std::max(14, lane.height() - 2));
        QColor fill = col;
        fill.setAlpha(150);
        p.setBrush(fill);
        p.setPen(QPen(Qt::white, 1.5, Qt::DashLine));
        p.drawRoundedRect(r, 4, 4);
        if (r.width() > 24) {
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(r.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(f).elidedText(label, Qt::ElideRight, int(r.width()) - 10));
        }
    };

    p.save();
    p.setClipRect(QRect(headerWidth(), rulerHeight(), viewport()->width(), viewport()->height()));

    // ---- moving clips
    if (m_drag == Drag::Move && m_dragMoved) {
        const Track *src = nullptr;
        pr.findClip(m_dragClip, &src);
        const bool audioDrag = src && src->kind == TrackKind::Audio;
        paintLaneHint(p, 1, m_dragLane > 0, audioDrag ? "+ New audio track (added at the bottom)" : "+ New video track");
        paintLaneHint(p, -1, m_dragLane < 0, audioDrag ? "+ New audio track" : "+ New video track (added at the top)");
        if (m_dragTargetTrack == kNewTrack && src) {
            const QRect lane = laneRect(m_dragLane).adjusted(0, 2, 0, -2);
            QRect gl = lane;
            gl.setHeight(std::min(lane.height(), 40));
            if (m_dragLane > 0)
                gl.setHeight(std::max(gl.height(), 18));
            for (const QString &id : m_dragIds) {
                const Track *t = nullptr;
                const Clip *c = pr.findClip(id, &t);
                if (c && t == src)
                    ghostRect(gl, c->start + m_dragDelta, c->duration(), c->name, colorFor(*c));
            }
        }
        if (m_dragNewAbove) {
            const int ri = rowOfTrack(m_dragTargetTrack);
            const Clip *c = pr.findClip(m_dragClip);
            if (ri >= 0 && c) {
                const Row &row = m_rows[ri];
                QRect lane(headerWidth(), rowTop(row) + 2, viewport()->width(), row.h - 4);
                lane.translate(0, pr.tracks[row.trackIndex].kind == TrackKind::Audio ? row.h / 2 : -row.h / 2);
                ghostRect(lane, c->start + m_dragDelta, c->duration(), c->name + " · new track", colorFor(*c));
            }
        }
        if (m_dragInsert && m_insertAt >= 0)
            insertMarker(rowOfTrack(m_dragTargetTrack.isEmpty() && src ? src->id : m_dragTargetTrack), m_insertAt);
    }

    // ---- reordering a track by its header
    if (m_drag == Drag::TrackReorder && m_dragMoved && m_reorderTo >= 0) {
        const int ri = rowOfTrack(pr.tracks[m_reorderTo].id);
        if (ri >= 0) {
            const Row &row = m_rows[ri];
            p.setClipping(false);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Theme::accent(), 2));
            p.drawRoundedRect(QRect(1, rowTop(row), viewport()->width() - 2, row.h), 4, 4);
        }
    }

    // ---- something dragged in from outside
    if (m_extDrag && m_dropTime >= 0) {
        const bool needsTarget = !m_dropLibraryId.isEmpty() && EffectsBrowser::dropKind(m_dropLibraryId) == "target";
        if (!needsTarget) {
            bool anyVideo = false, anyAudio = false;
            for (const DropGhost &g : m_dropGhost)
                (g.audio ? anyAudio : anyVideo) = true;
            const QString what = anyVideo && anyAudio ? "+ New tracks" : anyAudio ? "+ New audio track" : "+ New video track";
            paintLaneHint(p, 1, m_dropLane > 0, anyAudio && !anyVideo ? what + " (added at the bottom)" : what);
            paintLaneHint(p, -1, m_dropLane < 0, anyVideo && !anyAudio ? what + " (added at the top)" : what);
        }
        if (!m_dropTargetClip.isEmpty()) {
            const Track *t = nullptr;
            const Clip *c = pr.findClip(m_dropTargetClip, &t);
            const int ri = t ? rowOfTrack(t->id) : -1;
            if (c && ri >= 0) {
                p.setBrush(QColor(255, 255, 255, 30));
                p.setPen(QPen(Theme::accent(), 3));
                p.drawRoundedRect(clipRect(m_rows[ri], *c), 4, 4);
            }
        } else if (m_dropLibraryId.startsWith("tr:")) {
            if (m_dropRow >= 0) {
                const Track &t = pr.tracks[m_rows[m_dropRow].trackIndex];
                for (int i = 1; i < t.clips.size(); ++i)
                    if (std::llabs(t.clips[i].start - m_dropTime) < TimeUs(std::max(0.6, 40.0 / m_pxPerSec) * kUsPerSec)) {
                        const double x = xForTime(t.clips[i].start);
                        const int top = rowTop(m_rows[m_dropRow]);
                        p.setPen(Qt::NoPen);
                        p.setBrush(Theme::accent());
                        p.drawEllipse(QPointF(x, top + m_rows[m_dropRow].h / 2.0), 9, 9);
                        break;
                    }
            }
        } else if (!m_dropGhost.isEmpty()) {
            QRect lane;
            if (m_dropLane != 0) {
                lane = laneRect(m_dropLane).adjusted(0, 2, 0, -2);
                lane.setHeight(std::max(18, std::min(lane.height(), 40)));
            } else if (m_dropRow >= 0) {
                const Row &row = m_rows[m_dropRow];
                lane = QRect(headerWidth(), rowTop(row) + 2, viewport()->width(), row.h - 4);
                if (m_dropNewAbove) // straddle the edge where the new track will appear
                    lane.translate(0, pr.tracks[row.trackIndex].kind == TrackKind::Audio ? row.h / 2 : -row.h / 2);
                QColor hl = Theme::accent();
                hl.setAlpha(22);
                p.fillRect(QRect(headerWidth(), rowTop(row), viewport()->width(), row.h), hl);
            }
            if (!lane.isNull()) {
                TimeUs t = m_dropInsert && m_insertAt >= 0 ? m_insertAt : m_dropTime;
                for (const DropGhost &g : m_dropGhost) {
                    ghostRect(lane, t, g.duration, m_dropNewAbove ? g.label + " · new track" : g.label, g.color);
                    t += g.duration;
                }
            }
            if (m_dropInsert)
                insertMarker(m_dropRow, m_insertAt);
        } else if (m_dropRow >= 0) {
            const Row &row = m_rows[m_dropRow];
            p.setPen(QPen(Theme::accent(), 2));
            p.drawLine(QPointF(xForTime(m_dropTime), rowTop(row)), QPointF(xForTime(m_dropTime), rowTop(row) + row.h));
        }
    }
    p.restore();
}

void TimelineWidget::paintRuler(QPainter &p)
{
    const int rh = rulerHeight();
    p.fillRect(QRect(0, 0, viewport()->width(), rh), Theme::panel());
    p.setPen(QColor("#2E323B"));
    p.drawLine(0, rh - 1, viewport()->width(), rh - 1);
    static const double steps[] = {0.04, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200};
    double major = 1200;
    for (double s : steps)
        if (s * m_pxPerSec >= 90) {
            major = s;
            break;
        }
    const double minor = major / 5;
    const double tStart = std::max(0.0, timeForX(headerWidth()) / 1e6);
    const double tEnd = timeForX(viewport()->width()) / 1e6;
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    for (double t = std::floor(tStart / minor) * minor; t <= tEnd; t += minor) {
        const double x = xForTime(TimeUs(t * 1e6));
        if (x < headerWidth())
            continue;
        const bool isMajor = std::abs(std::remainder(t, major)) < minor * 0.01;
        p.setPen(isMajor ? Theme::textDim() : QColor("#3A3F4B"));
        p.drawLine(QPointF(x, rh - (isMajor ? 12 : 6)), QPointF(x, rh - 1));
        if (isMajor) {
            const qint64 ms = qint64(std::llround(t * 1000));
            QString label = QString("%1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0'));
            if (major < 1)
                label += QString(".%1").arg((ms % 1000) / 10, 2, 10, QChar('0'));
            p.drawText(QPointF(x + 3, 12), label);
        }
    }
    const Project &pr = m_doc->project();
    for (const Marker &mk : pr.markers) {
        const double x = xForTime(mk.t);
        if (x < headerWidth() || x > viewport()->width())
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(mk.color);
        QPolygonF flag{{x, double(rh - 1)}, {x - 5, double(rh - 9)}, {x - 5, double(rh - 15)}, {x + 5, double(rh - 15)}, {x + 5, double(rh - 9)}};
        p.drawPolygon(flag);
        if (!mk.label.isEmpty() && m_pxPerSec > 20) {
            p.setPen(mk.color);
            p.drawText(QPointF(x + 7, rh - 4), mk.label);
        }
    }
    p.setPen(Theme::textDim());
    p.drawText(QRect(6, 0, headerWidth() - 8, rh), Qt::AlignVCenter | Qt::AlignLeft,
               formatTimecode(m_doc->playhead(), pr.canvas.fps));
}

void TimelineWidget::paintHeader(QPainter &p, const Row &row)
{
    const Track &t = m_doc->project().tracks[row.trackIndex];
    const int top = rulerHeight() + row.y - verticalScrollBar()->value();
    const QRect r(0, top, headerWidth(), row.h);
    p.fillRect(r, Theme::panel());
    QColor accent = t.kind == TrackKind::Video ? Theme::clipVideo() : t.kind == TrackKind::Audio ? Theme::clipAudio()
                                                                                                 : Theme::clipCaption();
    p.fillRect(QRect(0, top, 4, row.h), accent);
    p.setPen(Theme::text());
    QString name = t.name;
    if (t.kind == TrackKind::Caption)
        name = "CC " + t.name;
    p.drawText(QRect(10, top, headerWidth() - 70, row.h), Qt::AlignVCenter | Qt::AlignLeft,
               fontMetrics().elidedText(name, Qt::ElideRight, headerWidth() - 72));
    const QStringList btns = headerButtons(t);
    for (int i = 0; i < btns.size(); ++i) {
        const QRect br(headerWidth() - 8 - (btns.size() - i) * 28, top + row.h / 2 - 10, 24, 20);
        bool on = false;
        if (btns[i] == "M")
            on = t.muted;
        else if (btns[i] == "L")
            on = t.locked;
        else if (btns[i] == "H")
            on = t.hidden;
        else if (btns[i] == "CC")
            on = t.burnIn;
        p.setPen(QColor("#3A3F4B"));
        p.setBrush(on ? (btns[i] == "CC" ? Theme::clipCaption() : Theme::danger()) : QColor("#2A2D35"));
        p.drawRoundedRect(br, 4, 4);
        p.setPen(on ? Qt::black : Theme::textDim());
        p.drawText(br, Qt::AlignCenter, btns[i]);
    }
}

void TimelineWidget::paintRow(QPainter &p, const Row &row)
{
    const Project &pr = m_doc->project();
    const Track &t = pr.tracks[row.trackIndex];
    const int top = rulerHeight() + row.y - verticalScrollBar()->value();
    if (top > viewport()->height() || top + row.h < rulerHeight())
        return;
    p.save();
    p.setClipRect(QRect(headerWidth(), rulerHeight(), viewport()->width(), viewport()->height()));
    p.fillRect(QRect(headerWidth(), top, viewport()->width(), row.h), t.locked ? QColor("#191A1F") : QColor("#16181D"));

    if (t.kind == TrackKind::Caption) {
        const auto &cues = m_cues.value(t.id);
        const Transcript *tr = pr.findTranscript(t.transcriptId);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        for (const CueInstance &c : cues) {
            const TimeUs shift = (m_drag == Drag::CueMove && m_dragMoved && m_dragCueTrack == t.id && m_dragCue == c.cueIndex)
                                     ? m_dragValue : 0;
            const double x0 = xForTime(c.start + shift), x1 = xForTime(c.end + shift);
            if (x1 < headerWidth() || x0 > viewport()->width())
                continue;
            QRectF r(x0, top + 3, std::max(2.0, x1 - x0 - 1), row.h - 6);
            const bool selected = m_doc->selectedCaptionTrack() == t.id && m_doc->selectedCue() == c.cueIndex;
            QColor col = Theme::clipCaption();
            col.setAlpha(t.hidden ? 80 : 200);
            p.setPen(selected ? QPen(Qt::white, 2) : QPen(col.darker(150)));
            p.setBrush(col.darker(selected ? 110 : 160));
            p.drawRoundedRect(r, 3, 3);
            bool lowConf = false;
            if (tr)
                for (const auto &w : c.words)
                    if (w.wordIndex >= 0 && w.wordIndex < tr->words.size() && tr->words[w.wordIndex].confidence >= 0 &&
                        tr->words[w.wordIndex].confidence < 0.5)
                        lowConf = true;
            if (lowConf)
                p.fillRect(QRectF(r.left(), r.bottom() - 3, r.width(), 3), QColor("#FF922B"));
            p.setPen(Qt::white);
            p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(f).elidedText(c.text(), Qt::ElideRight, int(r.width()) - 6));
        }
        if (cues.isEmpty()) {
            p.setPen(Theme::textDim());
            p.drawText(QRect(headerWidth() + 10, top, 600, row.h), Qt::AlignVCenter,
                       "No captions yet — generate them in the Subtitles panel");
        }
    } else {
        const bool dragging = (m_drag == Drag::Move || m_drag == Drag::TrimStart || m_drag == Drag::TrimEnd ||
                               m_drag == Drag::FadeIn || m_drag == Drag::FadeOut) && m_dragMoved;
        for (const Clip &c : t.clips) {
            const QRect r = clipRect(row, c);
            if (r.right() < headerWidth() || r.left() > viewport()->width())
                continue;
            paintClip(p, row, c, r, false);
        }
        // ghost previews of the edit in progress (applied as one undo step on release)
        if (dragging) {
            for (const QString &id : m_dragIds) {
                const Track *src = nullptr;
                const Clip *c = pr.findClip(id, &src);
                if (!c)
                    continue;
                Clip g = *c;
                QString destTrack = src->id;
                if (m_drag == Drag::Move) {
                    g.start += m_dragDelta;
                    if (id == m_dragClip && (m_dragTargetTrack == kNewTrack || m_dragNewAbove))
                        continue; // drawn in the new-track position by paintDropFeedback
                    if (id == m_dragClip && !m_dragTargetTrack.isEmpty())
                        destTrack = m_dragTargetTrack;
                } else if (m_drag == Drag::TrimStart && id == m_dragClip) {
                    const TimeUs d = m_dragValue - c->start;
                    g.start += d;
                    g.in += TimeUs(d * c->speed);
                } else if (m_drag == Drag::TrimEnd && id == m_dragClip) {
                    g.out += TimeUs((m_dragValue - c->end()) * c->speed);
                } else if ((m_drag == Drag::FadeIn || m_drag == Drag::FadeOut) && id == m_dragClip) {
                    const bool audioTrack = src->kind == TrackKind::Audio;
                    if (m_drag == Drag::FadeIn)
                        (audioTrack ? g.audio.fadeIn : g.fadeIn) = m_dragValue;
                    else
                        (audioTrack ? g.audio.fadeOut : g.fadeOut) = m_dragValue;
                } else if (m_drag != Drag::Move) {
                    continue;
                }
                if (destTrack != t.id)
                    continue;
                if (g.out <= g.in)
                    continue;
                paintClip(p, row, g, clipRect(row, g), true);
            }
        }
    }
    p.restore();
    paintHeader(p, row);
}

void TimelineWidget::paintClip(QPainter &p, const Row &row, const Clip &c, const QRect &r, bool ghost)
{
    const Project &pr = m_doc->project();
    const Track &t = pr.tracks[row.trackIndex];
    const bool selected = m_doc->selectedClips().contains(c.id);
    QColor base = colorFor(c);
    if (!c.enabled)
        base = QColor("#444852");
    p.save();
    if (ghost)
        p.setOpacity(0.55);
    QPainterPath path;
    path.addRoundedRect(QRectF(r), 4, 4);
    p.fillPath(path, base.darker(130));
    p.setClipPath(path, Qt::IntersectClip);

    const MediaAsset *m = c.type == ClipType::Media ? pr.findMedia(c.mediaId) : nullptr;
    if (m && !m->missing && t.kind == TrackKind::Video && !m_compact) {
        const QImage strip = m_cache->filmstrip(*m);
        if (!strip.isNull() && m->kind == MediaKind::Video && m->duration > 0) {
            const double x0 = double(c.in) / m->duration * strip.width();
            const double x1 = double(c.out) / m->duration * strip.width();
            p.setOpacity(ghost ? 0.4 : 0.75);
            p.drawImage(QRectF(r.adjusted(0, 14, 0, 0)), strip, QRectF(x0, 0, std::max(1.0, x1 - x0), strip.height()));
            p.setOpacity(ghost ? 0.55 : 1.0);
        } else if (!strip.isNull()) {
            p.setOpacity(0.7);
            const QImage thumb = strip.scaledToHeight(std::max(8, r.height() - 14), Qt::SmoothTransformation);
            for (int x = r.left(); x < r.right(); x += thumb.width() + 2)
                p.drawImage(QPoint(x, r.top() + 14), thumb);
            p.setOpacity(ghost ? 0.55 : 1.0);
        }
    }
    if (m && !m->missing && t.kind == TrackKind::Audio) {
        const QVector<quint8> &pk = m_cache->peaks(*m);
        if (!pk.isEmpty()) {
            p.setPen(QColor(255, 255, 255, 150));
            const int mid = r.center().y() + 5;
            const int amp = (r.height() - 16) / 2;
            const int x0 = std::max(r.left(), headerWidth());
            const int x1 = std::min(r.right(), viewport()->width());
            const double gain = std::pow(10.0, c.audio.gainDb / 20.0);
            for (int x = x0; x < x1; ++x) {
                const double tl0 = (x - r.left()) / m_pxPerSec, tl1 = (x + 1 - r.left()) / m_pxPerSec;
                const double s0 = c.in / 1e6 + tl0 * c.speed, s1 = c.in / 1e6 + tl1 * c.speed;
                int i0 = int(s0 * MediaCache::kPeaksPerSecond), i1 = std::max(i0 + 1, int(s1 * MediaCache::kPeaksPerSecond));
                int v = 0;
                for (int i = std::max(0, i0); i < std::min(int(pk.size()), i1); ++i)
                    v = std::max(v, int(pk[i]));
                const int hgt = int(std::min(1.0, v / 255.0 * gain) * amp);
                p.drawLine(x, mid - hgt, x, mid + hgt);
            }
        }
    }
    if (c.type == ClipType::Title || c.type == ClipType::Shape || c.type == ClipType::Effect) {
        p.setPen(QColor(255, 255, 255, 200));
        QString what;
        if (c.type == ClipType::Title)
            what = "T  " + c.title.text.simplified();
        else if (c.type == ClipType::Shape)
            what = "■  " + c.shape.shape;
        else {
            QStringList fx;
            for (const EffectSpec &e : c.fx.effects)
                fx << ImageFx::effectLabel(e.type);
            if (!c.fx.look.isEmpty())
                fx << "filter";
            what = "✦  " + (fx.isEmpty() ? QString("adjustment") : fx.join(", "));
        }
        p.drawText(r.adjusted(6, 14, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, what);
    }
    // fades
    const bool audioTrack = t.kind == TrackKind::Audio;
    const TimeUs fin = audioTrack ? c.audio.fadeIn : c.fadeIn;
    const TimeUs fout = audioTrack ? c.audio.fadeOut : c.fadeOut;
    if (fin > 0) {
        QPainterPath tri;
        tri.moveTo(r.left(), r.top());
        tri.lineTo(r.left() + fin / 1e6 * m_pxPerSec, r.top());
        tri.lineTo(r.left(), r.bottom());
        tri.closeSubpath();
        p.fillPath(tri, QColor(0, 0, 0, 110));
    }
    if (fout > 0) {
        QPainterPath tri;
        tri.moveTo(r.right(), r.top());
        tri.lineTo(r.right() - fout / 1e6 * m_pxPerSec, r.top());
        tri.lineTo(r.right(), r.bottom());
        tri.closeSubpath();
        p.fillPath(tri, QColor(0, 0, 0, 110));
    }
    if (m && m->missing) {
        p.fillRect(r, QBrush(QColor(229, 72, 77, 90), Qt::BDiagPattern));
    }
    // label bar
    p.fillRect(QRect(r.left(), r.top(), r.width(), 14), base);
    p.setPen(Qt::white);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    p.setFont(f);
    QString label = c.name;
    if (std::abs(c.speed - 1.0) > 1e-3)
        label += QString("  %1×").arg(c.speed, 0, 'g', 3);
    if (c.fx.speedCurve.isSet())
        label += "  ⤳ curve";
    if (c.type != ClipType::Effect && (!c.fx.effects.isEmpty() || !c.fx.look.isEmpty() || !c.fx.adjust.isIdentity()))
        label = "fx · " + label;
    if (c.fx.chroma.enabled || c.fx.mask.isSet())
        label = "✂ " + label;
    if (!c.fx.audio.isDefault())
        label = "🎙 " + label;
    if (!c.linkId.isEmpty())
        label = "⧉ " + label;
    if (m && m->missing)
        label = "OFFLINE · " + label;
    p.drawText(r.adjusted(5, 0, -4, 0).topLeft() + QPoint(0, 11), QFontMetrics(f).elidedText(label, Qt::ElideRight, r.width() - 10));
    // keyframe diamonds along the bottom edge
    if (!c.fx.keyframes.isEmpty() && r.height() > 18) {
        p.setPen(QPen(QColor(20, 20, 20, 200), 1));
        const TimeUs ph = m_doc->playhead();
        for (const Keyframe &k : c.fx.keyframes) {
            const double x = r.left() + k.t / 1e6 * m_pxPerSec;
            const double y = r.bottom() - 6;
            const bool here = std::llabs(c.start + k.t - ph) <= 20 * kUsPerMs;
            p.setBrush(here ? QColor("#FFD43B") : QColor(255, 255, 255, 230));
            QPolygonF d{{x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}};
            p.drawPolygon(d);
        }
    }
    p.setClipping(false);
    // transition marker at the clip start (bow tie straddling the cut)
    if (c.fx.transitionIn.isSet()) {
        const double half = c.fx.transitionIn.duration / 2e6 * m_pxPerSec;
        const QRectF tr(r.left() - half, r.top() + 15, std::max(10.0, 2 * half), std::max(8, r.height() - 17));
        QPainterPath bow;
        bow.moveTo(tr.topLeft());
        bow.lineTo(tr.bottomRight());
        bow.lineTo(tr.topRight());
        bow.lineTo(tr.bottomLeft());
        bow.closeSubpath();
        p.setPen(QPen(QColor(255, 255, 255, 220), 1));
        p.setBrush(QColor(91, 140, 255, 170));
        p.drawRoundedRect(tr, 3, 3);
        p.setPen(Qt::white);
        QFont tf = font();
        tf.setPointSizeF(tf.pointSizeF() * 0.75);
        p.setFont(tf);
        p.drawText(tr, Qt::AlignCenter, "⇄");
    }
    // fade handles
    if (r.width() > 30 && !ghost) {
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(r.left() + fin / 1e6 * m_pxPerSec, r.top() + 5), 3, 3);
        p.drawEllipse(QPointF(r.right() - fout / 1e6 * m_pxPerSec, r.top() + 5), 3, 3);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(selected ? QPen(Theme::isHighContrast() ? Qt::white : QColor("#FFFFFF"), 2) : QPen(base.lighter(130), 1));
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.restore();
}

// ------------------------------------------------------------------ interaction

TimeUs TimelineWidget::snapTime(TimeUs t, const QStringList &exclude, bool *snapped) const
{
    if (!m_snap || (QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        if (snapped)
            *snapped = false;
        return t;
    }
    const auto pts = TimelineOps::snapPoints(m_doc->project(), exclude);
    const TimeUs tol = TimeUs(8.0 / m_pxPerSec * 1e6);
    return TimelineOps::snap(pts, t, tol, snapped);
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_doc->isOpen())
        return;
    setFocus();
    m_pressPos = e->pos();
    m_dragMoved = false;
    m_drag = Drag::None;
    if (e->button() != Qt::LeftButton)
        return;
    if (e->pos().y() < rulerHeight() && e->pos().x() > headerWidth()) {
        m_drag = Drag::Scrub;
        emit seekRequested(std::max<TimeUs>(0, timeForX(e->pos().x())));
        return;
    }
    const Hit h = hitTest(e->pos());
    if (h.header) {
        if (!h.headerButton.isEmpty())
            toggleHeaderButton(m_rows[h.row].trackIndex, h.headerButton);
        else if (h.row >= 0) {
            // drag a track by its header to reorder it among tracks of the same kind
            m_drag = Drag::TrackReorder;
            m_reorderTrack = m_rows[h.row].trackIndex;
            m_reorderTo = -1;
        }
        return;
    }
    if (h.row >= 0 && h.cueIndex >= 0) {
        const Track &t = m_doc->project().tracks[m_rows[h.row].trackIndex];
        m_doc->setSelectedCue(t.id, h.cueIndex);
        m_dragCueTrack = t.id;
        m_dragCue = h.cueIndex;
        if (h.cueEdgeStart)
            m_drag = Drag::CueStart;
        else if (h.cueEdgeEnd)
            m_drag = Drag::CueEnd;
        else {
            // drag the caption block to retime it; a plain click just seeks
            m_drag = Drag::CueMove;
            m_dragValue = 0;
            m_cueOrigStart = m_cueOrigEnd = 0;
            for (const CueInstance &ci : m_cues.value(t.id))
                if (ci.cueIndex == h.cueIndex) {
                    m_cueOrigStart = ci.start;
                    m_cueOrigEnd = ci.end;
                    break;
                }
            emit seekRequested(timeForX(e->pos().x()));
        }
        return;
    }
    if (h.clipId.isEmpty()) {
        if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier)))
            m_doc->setSelectedClips({});
        m_drag = Drag::Scrub;
        emit seekRequested(std::max<TimeUs>(0, timeForX(e->pos().x())));
        return;
    }
    // selection
    QStringList sel = m_doc->selectedClips();
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        if (sel.contains(h.clipId))
            sel.removeAll(h.clipId);
        else
            sel << h.clipId;
    } else if (e->modifiers() & Qt::ShiftModifier) {
        if (!sel.contains(h.clipId))
            sel << h.clipId;
    } else if (!sel.contains(h.clipId)) {
        sel = {h.clipId};
    }
    m_doc->setSelectedClips(sel);

    const Track *tr = nullptr;
    const Clip *c = m_doc->project().findClip(h.clipId, &tr);
    if (!c || tr->locked) {
        emit statusMessage("Track is locked");
        return;
    }
    m_dragClip = h.clipId;
    m_dragLinked = !(e->modifiers() & Qt::AltModifier);
    m_dragTargetTrack.clear();
    m_dragDelta = 0;
    switch (h.part) {
    case Part::EdgeStart:
        m_drag = Drag::TrimStart;
        m_dragValue = c->start;
        m_dragIds = {h.clipId};
        break;
    case Part::EdgeEnd:
        m_drag = Drag::TrimEnd;
        m_dragValue = c->end();
        m_dragIds = {h.clipId};
        break;
    case Part::FadeIn:
        m_drag = Drag::FadeIn;
        m_dragValue = tr->kind == TrackKind::Audio ? c->audio.fadeIn : c->fadeIn;
        m_dragIds = {h.clipId};
        break;
    case Part::FadeOut:
        m_drag = Drag::FadeOut;
        m_dragValue = tr->kind == TrackKind::Audio ? c->audio.fadeOut : c->fadeOut;
        m_dragIds = {h.clipId};
        break;
    default: {
        m_drag = Drag::Move;
        QStringList ids;
        for (const QString &id : m_doc->selectedClips()) {
            if (m_dragLinked) {
                for (Clip *lc : const_cast<Project &>(m_doc->project()).linkedClips(id))
                    if (!ids.contains(lc->id))
                        ids << lc->id;
            } else if (!ids.contains(id)) {
                ids << id;
            }
        }
        m_dragIds = ids;
    }
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_doc->isOpen())
        return;
    if (m_drag == Drag::None) {
        const Hit h = hitTest(e->pos());
        Qt::CursorShape cur = Qt::ArrowCursor;
        if (h.part == Part::EdgeStart || h.part == Part::EdgeEnd || h.cueEdgeStart || h.cueEdgeEnd)
            cur = Qt::SizeHorCursor;
        else if (h.part == Part::FadeIn || h.part == Part::FadeOut)
            cur = Qt::PointingHandCursor;
        else if (h.part == Part::Body || h.cueIndex >= 0)
            cur = Qt::OpenHandCursor;
        else if (h.header && h.headerButton.isEmpty() && h.row >= 0)
            cur = Qt::OpenHandCursor;
        viewport()->setCursor(cur);
        return;
    }
    if (!m_dragMoved && (e->pos() - m_pressPos).manhattanLength() < 3)
        return;
    if (!m_dragMoved && m_drag != Drag::Scrub)
        viewport()->setCursor(Qt::ClosedHandCursor);
    m_dragMoved = true;
    if (!m_autoScroll->isActive())
        m_autoScroll->start();
    handleDragMove(e->pos(), e->modifiers());
}

void TimelineWidget::handleDragMove(const QPoint &pos, Qt::KeyboardModifiers mods)
{
    m_lastDragPos = pos;
    m_lastMods = mods;
    const Project &p = m_doc->project();
    const TimeUs t = std::max<TimeUs>(0, timeForX(pos.x()));
    m_snapLine = -1;
    bool snapped = false;
    switch (m_drag) {
    case Drag::Scrub:
        emit seekRequested(t);
        break;
    case Drag::Move:
        updateMoveDrag(pos, mods);
        break;
    case Drag::TrackReorder: {
        const int ri = rowAtY(pos.y());
        m_reorderTo = -1;
        if (ri >= 0 && m_reorderTrack >= 0 && m_reorderTrack < p.tracks.size()) {
            const int ti = m_rows[ri].trackIndex;
            if (p.tracks[ti].kind == p.tracks[m_reorderTrack].kind && ti != m_reorderTrack)
                m_reorderTo = ti;
        }
        emit statusMessage(m_reorderTo >= 0 ? QString("Move track %1 to the position of %2").arg(p.tracks[m_reorderTrack].name, p.tracks[m_reorderTo].name)
                                            : QString("Drag onto another track of the same kind to reorder"));
        break;
    }
    case Drag::CueMove: {
        TimeUs d = TimeUs((pos.x() - m_pressPos.x()) / m_pxPerSec * 1e6);
        d = std::max(d, -m_cueOrigStart);
        const TimeUs s2 = snapTime(m_cueOrigStart + d, {}, &snapped);
        if (snapped) {
            d = s2 - m_cueOrigStart;
            m_snapLine = s2;
        }
        m_dragValue = d;
        emit statusMessage(QString("Caption %1%2").arg(d >= 0 ? "+" : "-").arg(formatClock(std::llabs(d))));
        break;
    }
    case Drag::TrimStart:
    case Drag::TrimEnd: {
        TimeUs v = snapTime(t, m_dragIds, &snapped);
        if (snapped)
            m_snapLine = v;
        m_dragValue = v;
        emit statusMessage(QString("Trim to %1").arg(formatClock(v)));
        break;
    }
    case Drag::FadeIn:
    case Drag::FadeOut: {
        const Clip *c = p.findClip(m_dragClip);
        if (!c)
            break;
        TimeUs v = m_drag == Drag::FadeIn ? t - c->start : c->end() - t;
        m_dragValue = std::clamp<TimeUs>(v, 0, c->duration());
        emit statusMessage(QString("Fade %1").arg(formatClock(m_dragValue)));
        break;
    }
    case Drag::CueStart:
    case Drag::CueEnd: {
        TimeUs v = snapTime(t, {}, &snapped);
        if (snapped)
            m_snapLine = v;
        m_dragValue = v;
        emit statusMessage(QString("Caption %1 → %2").arg(m_drag == Drag::CueStart ? "start" : "end", formatClock(v)));
        break;
    }
    default:
        break;
    }
    viewport()->update();
}

void TimelineWidget::updateMoveDrag(const QPoint &pos, Qt::KeyboardModifiers mods)
{
    const Project &p = m_doc->project();
    bool snapped = false;
    TimeUs delta = TimeUs((pos.x() - m_pressPos.x()) / m_pxPerSec * 1e6);
    const Track *src = nullptr;
    const Clip *c = p.findClip(m_dragClip, &src);
    if (!c)
        return;
    // snap either edge of the primary clip
    const TimeUs s = snapTime(c->start + delta, m_dragIds, &snapped);
    if (snapped) {
        delta = s - c->start;
        m_snapLine = s;
    } else {
        const TimeUs en = snapTime(c->end() + delta, m_dragIds, &snapped);
        if (snapped) {
            delta = en - c->end();
            m_snapLine = en;
        }
    }
    TimeUs minStart = c->start;
    for (const QString &id : m_dragIds)
        if (const Clip *dc = p.findClip(id))
            minStart = std::min(minStart, dc->start);
    delta = std::max(delta, -minStart);
    m_dragDelta = delta;

    // where to: another track of the same kind, or a "new track" lane
    m_dragLane = laneAtY(pos.y());
    m_dragTargetTrack.clear();
    if (m_dragLane != 0) {
        m_dragTargetTrack = kNewTrack;
    } else if (const int ri = rowAtY(pos.y()); ri >= 0) {
        const Track &tgt = p.tracks[m_rows[ri].trackIndex];
        if (src && tgt.kind == src->kind && tgt.id != src->id && !tgt.locked)
            m_dragTargetTrack = tgt.id;
    }
    // Ctrl/Cmd: insert, pushing the clips at the drop point later
    m_dragInsert = (mods & (Qt::ControlModifier | Qt::MetaModifier)) && m_dragTargetTrack != kNewTrack;
    m_dragNewAbove = false;
    if (!m_dragInsert && !m_dragTargetTrack.isEmpty() && m_dragTargetTrack != kNewTrack)
        if (const Track *tt = p.findTrack(m_dragTargetTrack))
            m_dragNewAbove = !isRangeFree(*tt, c->start + delta, c->duration(), m_dragIds);
    m_insertAt = -1;
    if (m_dragInsert) {
        const Track *tt = m_dragTargetTrack.isEmpty() ? src : p.findTrack(m_dragTargetTrack);
        TimeUs gStart = c->start;
        for (const QString &id : m_dragIds) {
            const Track *t2 = nullptr;
            if (const Clip *dc = p.findClip(id, &t2); dc && t2 == src)
                gStart = std::min(gStart, dc->start);
        }
        if (tt)
            m_insertAt = insertPoint(*tt, gStart + delta, m_dragIds);
    }
    QString msg = QString("Move %1%2").arg(delta >= 0 ? "+" : "-").arg(formatClock(std::llabs(delta)));
    if (m_dragTargetTrack == kNewTrack)
        msg += " · to a new track";
    else if (m_dragInsert)
        msg += QString(" · insert at %1 (clips after it move later)").arg(formatClock(m_insertAt));
    else if (m_dragNewAbove)
        msg += " · that spot is taken, so it goes on a new track";
    else
        msg += " · hold Ctrl/⌘ to insert and push clips aside";
    emit statusMessage(msg);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *e)
{
    Q_UNUSED(e);
    const Drag d = m_drag;
    m_drag = Drag::None;
    m_snapLine = -1;
    m_autoScroll->stop();
    viewport()->unsetCursor();
    const QString moveTarget = m_dragTargetTrack;
    const bool moveInsert = m_dragInsert;
    m_dragLane = 0;
    m_dragInsert = false;
    m_insertAt = -1;
    const int reorderFrom = m_reorderTrack, reorderTo = m_reorderTo;
    m_reorderTrack = m_reorderTo = -1;
    if (!m_dragMoved || !m_doc->isOpen()) {
        viewport()->update();
        return;
    }
    const QString clipId = m_dragClip;
    switch (d) {
    case Drag::Move: {
        const QStringList ids = m_dragIds;
        const TimeUs delta = m_dragDelta;
        const bool linked = m_dragLinked;
        const PlaceMode mode = moveInsert ? PlaceMode::Insert : PlaceMode::Auto;
        if (!m_doc->apply(moveInsert ? "Insert clip" : "Move clip", [&](Project &p) {
                return TimelineOps::moveClipsPlaced(p, ids, clipId, delta, moveTarget, mode, linked);
            }))
            emit statusMessage("Couldn't move there (locked track?)");
        else
            emit statusMessage(moveTarget == kNewTrack ? "Moved to a new track" : moveInsert ? "Inserted" : "Moved");
        break;
    }
    case Drag::TrackReorder: {
        if (reorderFrom < 0 || reorderTo < 0)
            break;
        const QString tid = m_doc->project().tracks[reorderFrom].id;
        m_doc->apply("Reorder tracks", [&](Project &p) { return TimelineOps::moveTrackTo(p, tid, reorderTo); });
        break;
    }
    case Drag::CueMove: {
        const QString trackId = m_dragCueTrack;
        const int cue = m_dragCue;
        const TimeUs d = m_dragValue;
        if (d == 0)
            break;
        const TimeUs ns = m_cueOrigStart + d;
        if (!m_doc->apply("Move caption", [&](Project &p) {
                Track *t = p.findTrack(trackId);
                Transcript *tr = t ? p.findTranscript(t->transcriptId) : nullptr;
                if (!tr)
                    return false;
                TimeUs s0;
                if (!CaptionMapper::timelineToTranscript(p, *tr, ns, &s0))
                    return false;
                return TranscriptEdit::moveCue(*tr, cue, s0 - tr->cueStart(cue));
            }))
            emit statusMessage("The caption can't move there (it would overlap another caption or leave its clip)");
        break;
    }
    case Drag::TrimStart:
    case Drag::TrimEnd: {
        const TimeUs v = m_dragValue;
        const bool linked = m_dragLinked;
        m_doc->apply("Trim clip", [&](Project &p) {
            const Clip *c = p.findClip(clipId);
            const TimeUs before = d == Drag::TrimStart ? c->start : c->end();
            const TimeUs after = TimelineOps::trimClip(p, clipId, d == Drag::TrimStart ? Edge::Start : Edge::End, v, linked);
            return after != before;
        });
        break;
    }
    case Drag::FadeIn:
    case Drag::FadeOut: {
        const TimeUs v = snapToFrame(m_dragValue, m_doc->project().canvas.fps);
        m_doc->apply(d == Drag::FadeIn ? "Fade in" : "Fade out", [&](Project &p) {
            Track *tr = nullptr;
            Clip *c = p.findClip(clipId, &tr);
            if (!c)
                return false;
            const bool audio = tr->kind == TrackKind::Audio;
            if (d == Drag::FadeIn)
                (audio ? c->audio.fadeIn : c->fadeIn) = v;
            else
                (audio ? c->audio.fadeOut : c->fadeOut) = v;
            return true;
        });
        break;
    }
    case Drag::CueStart:
    case Drag::CueEnd: {
        const QString trackId = m_dragCueTrack;
        const int cue = m_dragCue;
        const TimeUs v = m_dragValue;
        m_doc->apply("Adjust caption timing", [&](Project &p) {
            Track *t = p.findTrack(trackId);
            if (!t)
                return false;
            Transcript *tr = p.findTranscript(t->transcriptId);
            if (!tr)
                return false;
            TimeUs src;
            if (!CaptionMapper::timelineToTranscript(p, *tr, v, &src))
                return false;
            const TimeUs s = d == Drag::CueStart ? src : tr->cueStart(cue);
            const TimeUs en = d == Drag::CueEnd ? src : tr->cueEnd(cue);
            return TranscriptEdit::setCueTiming(*tr, cue, s, en);
        });
        break;
    }
    default:
        break;
    }
    m_dragIds.clear();
    viewport()->update();
}

void TimelineWidget::keyPressEvent(QKeyEvent *e)
{
    if (m_drag != Drag::None && e->key() == Qt::Key_Escape) {
        m_drag = Drag::None;
        m_dragMoved = false;
        m_snapLine = -1;
        m_dragLane = 0;
        m_dragInsert = false;
        m_reorderTrack = m_reorderTo = -1;
        m_autoScroll->stop();
        viewport()->unsetCursor();
        emit statusMessage("Drag cancelled");
        viewport()->update();
        return;
    }
    if (m_drag == Drag::Move && m_dragMoved && (e->key() == Qt::Key_Control || e->key() == Qt::Key_Meta)) {
        handleDragMove(m_lastDragPos, e->modifiers());
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void TimelineWidget::keyReleaseEvent(QKeyEvent *e)
{
    if (m_drag == Drag::Move && m_dragMoved && (e->key() == Qt::Key_Control || e->key() == Qt::Key_Meta)) {
        handleDragMove(m_lastDragPos, e->modifiers());
        viewport()->update();
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(e);
}

void TimelineWidget::autoScrollStep()
{
    if (m_drag == Drag::None && !m_extDrag) {
        m_autoScroll->stop();
        return;
    }
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    const int w = viewport()->width(), h = viewport()->height();
    auto speed = [](int dist, int zone) { return dist >= zone ? 0 : 4 + (zone - dist) * 24 / zone; };
    int dx = 0, dy = 0;
    const int zone = 36;
    if (pos.x() < headerWidth() + zone && pos.x() > headerWidth() - 60 && m_drag != Drag::TrackReorder)
        dx = -speed(pos.x() - headerWidth(), zone);
    else if (pos.x() > w - zone && pos.x() < w + 60 && m_drag != Drag::TrackReorder)
        dx = speed(w - pos.x(), zone);
    if (pos.y() < rulerHeight() + 20 && pos.y() > rulerHeight() - 40 && m_drag != Drag::Scrub)
        dy = -speed(pos.y() - rulerHeight(), 20);
    else if (pos.y() > h - 20 && pos.y() < h + 40 && m_drag != Drag::Scrub)
        dy = speed(h - pos.y(), 20);
    if (!dx && !dy)
        return;
    QScrollBar *hs = horizontalScrollBar(), *vs = verticalScrollBar();
    const int ox = hs->value(), oy = vs->value();
    if (dx > 0 && hs->value() >= hs->maximum())
        hs->setMaximum(hs->maximum() + dx); // keep going past the end of the project
    hs->setValue(hs->value() + dx);
    vs->setValue(vs->value() + dy);
    if (hs->value() == ox && vs->value() == oy)
        return;
    if (m_extDrag)
        updateExternalDrag(pos, nullptr, m_lastMods);
    else if (m_dragMoved)
        handleDragMove(pos, QApplication::keyboardModifiers());
    viewport()->update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    const Hit h = hitTest(e->pos());
    if (h.header && h.headerButton.isEmpty() && h.row >= 0) {
        const int ti = m_rows[h.row].trackIndex;
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename track", "Track name:", QLineEdit::Normal,
                                                   m_doc->project().tracks[ti].name, &ok);
        if (ok && !name.trimmed().isEmpty()) {
            const QString id = m_doc->project().tracks[ti].id;
            m_doc->apply("Rename track", [&](Project &p) {
                p.findTrack(id)->name = name.trimmed();
                return true;
            });
        }
        return;
    }
    if (!h.clipId.isEmpty())
        emit clipActivated(h.clipId);
    else if (h.cueIndex >= 0)
        emit cueActivated(m_doc->project().tracks[m_rows[h.row].trackIndex].id, h.cueIndex);
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
    const QPoint d = e->angleDelta();
    if (e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const TimeUs anchor = timeForX(e->position().x());
        const double factor = d.y() > 0 ? 1.2 : 1 / 1.2;
        m_pxPerSec = std::clamp(m_pxPerSec * factor, 2.0, 2000.0);
        updateScrollbars();
        horizontalScrollBar()->setValue(int(anchor / 1e6 * m_pxPerSec - (e->position().x() - headerWidth())));
        viewport()->update();
        return;
    }
    if ((e->modifiers() & Qt::ShiftModifier) || std::abs(d.x()) > std::abs(d.y())) {
        const int dx = std::abs(d.x()) > 0 ? d.x() : d.y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        return;
    }
    verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y() / 2);
}

void TimelineWidget::zoomIn()
{
    m_pxPerSec = std::min(2000.0, m_pxPerSec * 1.4);
    updateScrollbars();
    ensurePlayheadVisible(true);
    viewport()->update();
}

void TimelineWidget::zoomOut()
{
    m_pxPerSec = std::max(2.0, m_pxPerSec / 1.4);
    updateScrollbars();
    ensurePlayheadVisible(true);
    viewport()->update();
}

void TimelineWidget::zoomToFit()
{
    const TimeUs dur = std::max<TimeUs>(m_doc->project().duration(), 10 * kUsPerSec);
    m_pxPerSec = std::clamp((viewport()->width() - headerWidth() - 40) / (dur / 1e6), 2.0, 2000.0);
    updateScrollbars();
    horizontalScrollBar()->setValue(0);
    viewport()->update();
}

void TimelineWidget::ensurePlayheadVisible(bool page)
{
    const double x = xForTime(m_doc->playhead());
    const int left = headerWidth() + 20, right = viewport()->width() - 40;
    if (x < left || x > right) {
        const double target = m_doc->playhead() / 1e6 * m_pxPerSec - (page ? (right - left) * 0.15 : (right - left) / 2.0);
        horizontalScrollBar()->setValue(int(std::max(0.0, target)));
    }
}

void TimelineWidget::toggleHeaderButton(int ti, const QString &btn)
{
    const QString id = m_doc->project().tracks[ti].id;
    m_doc->apply("Toggle track " + btn, [&](Project &p) {
        Track *t = p.findTrack(id);
        if (btn == "M")
            t->muted = !t->muted;
        else if (btn == "L")
            t->locked = !t->locked;
        else if (btn == "H")
            t->hidden = !t->hidden;
        else if (btn == "CC")
            t->burnIn = !t->burnIn;
        return true;
    });
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_doc->isOpen())
        return;
    const Hit h = hitTest(e->pos());
    if (h.header && h.row >= 0)
        return showTrackMenu(e->globalPos(), m_rows[h.row].trackIndex);
    if (!h.clipId.isEmpty()) {
        if (!m_doc->selectedClips().contains(h.clipId))
            m_doc->setSelectedClips({h.clipId});
        return showClipMenu(e->globalPos(), h.clipId);
    }
    showEmptyMenu(e->globalPos(), std::max<TimeUs>(0, timeForX(e->pos().x())), h.row >= 0 ? m_rows[h.row].trackIndex : -1);
}

void TimelineWidget::showClipMenu(const QPoint &global, const QString &clipId)
{
    QMenu menu(this);
    const QStringList sel = m_doc->selectedClips();
    const TimeUs ph = m_doc->playhead();
    menu.addAction("Split at playhead", [this, clipId, ph]() {
        m_doc->apply("Split", [&](Project &p) { return TimelineOps::splitClip(p, clipId, ph); });
    });
    menu.addAction("Delete", [this, sel]() {
        m_doc->apply("Delete", [&](Project &p) { return TimelineOps::deleteClips(p, sel, false); });
    });
    menu.addAction("Ripple delete", [this, sel]() {
        m_doc->apply("Ripple delete", [&](Project &p) { return TimelineOps::deleteClips(p, sel, true); });
    });
    menu.addAction("Duplicate", [this, clipId]() {
        m_doc->apply("Duplicate", [&](Project &p) {
            Track *t = nullptr;
            const Clip *c = p.findClip(clipId, &t);
            if (!c)
                return false;
            return !TimelineOps::insertClipCopy(p, t->id, *c, c->end()).isEmpty();
        });
    });
    menu.addSeparator();
    const Clip *c = m_doc->project().findClip(clipId);
    if (c && !c->linkId.isEmpty())
        menu.addAction("Unlink audio/video", [this, sel]() {
            m_doc->apply("Unlink", [&](Project &p) { return TimelineOps::unlinkClips(p, sel); });
        });
    else if (sel.size() > 1)
        menu.addAction("Link selected", [this, sel]() {
            m_doc->apply("Link", [&](Project &p) { return TimelineOps::linkClips(p, sel); });
        });
    menu.addAction(c && c->enabled ? "Disable clip" : "Enable clip", [this, sel]() {
        m_doc->apply("Toggle enabled", [&](Project &p) {
            for (const QString &id : sel)
                if (Clip *cc = p.findClip(id))
                    cc->enabled = !cc->enabled;
            return true;
        });
    });
    QMenu *fades = menu.addMenu("Fades");
    for (double secs : {0.25, 0.5, 1.0, 2.0})
        fades->addAction(QString("Fade in + out %1 s").arg(secs), [this, sel, secs]() {
            m_doc->apply("Add fades", [&](Project &p) {
                for (const QString &id : sel) {
                    Track *t = nullptr;
                    Clip *cc = p.findClip(id, &t);
                    const TimeUs f = std::min<TimeUs>(TimeUs(secs * kUsPerSec), cc->duration() / 2);
                    if (t->kind == TrackKind::Audio)
                        cc->audio.fadeIn = cc->audio.fadeOut = f;
                    else
                        cc->fadeIn = cc->fadeOut = f;
                }
                return true;
            });
        });
    fades->addAction("Remove fades", [this, sel]() {
        m_doc->apply("Remove fades", [&](Project &p) {
            for (const QString &id : sel)
                if (Clip *cc = p.findClip(id))
                    cc->fadeIn = cc->fadeOut = cc->audio.fadeIn = cc->audio.fadeOut = 0;
            return true;
        });
    });
    if (c && c->type == ClipType::Media) {
        QMenu *speed = menu.addMenu("Speed");
        for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0})
            speed->addAction(QString("%1×").arg(s), [this, clipId, s]() {
                if (!m_doc->apply("Change speed", [&](Project &p) { return TimelineOps::setClipSpeed(p, clipId, s); }))
                    emit statusMessage("Not enough room after the clip for that speed");
            });
    }
    menu.exec(global);
}

void TimelineWidget::showTrackMenu(const QPoint &global, int ti)
{
    QMenu menu(this);
    const Track t = m_doc->project().tracks[ti];
    menu.addAction("Rename…", [this, t]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Rename track", "Track name:", QLineEdit::Normal, t.name, &ok);
        if (ok && !name.trimmed().isEmpty())
            m_doc->apply("Rename track", [&](Project &p) {
                p.findTrack(t.id)->name = name.trimmed();
                return true;
            });
    });
    if (t.kind != TrackKind::Caption) {
        menu.addAction("Move up", [this, t]() {
            m_doc->apply("Move track", [&](Project &p) { return TimelineOps::moveTrack(p, t.id, t.kind == TrackKind::Video ? 1 : -1); });
        });
        menu.addAction("Move down", [this, t]() {
            m_doc->apply("Move track", [&](Project &p) { return TimelineOps::moveTrack(p, t.id, t.kind == TrackKind::Video ? -1 : 1); });
        });
    }
    if (t.kind == TrackKind::Audio) {
        QMenu *vol = menu.addMenu("Track volume");
        for (double db : {-12.0, -6.0, -3.0, 0.0, 3.0, 6.0})
            vol->addAction(QString("%1 dB").arg(db), [this, t, db]() {
                m_doc->apply("Track volume", [&](Project &p) {
                    p.findTrack(t.id)->volumeDb = db;
                    return true;
                });
            });
    }
    if (t.kind == TrackKind::Caption) {
        QMenu *styles = menu.addMenu("Caption template");
        for (const CaptionStyle &s : TemplateLibrary::instance().all())
            styles->addAction(s.name, [this, t, s]() {
                m_doc->apply("Apply caption template", [&](Project &p) {
                    Track *tt = p.findTrack(t.id);
                    tt->styleId = s.id;
                    if (Transcript *tr = p.findTranscript(tt->transcriptId))
                        TranscriptEdit::resegment(*tr, s.withOverrides(tt->styleOverrides));
                    return true;
                });
            });
    }
    menu.addSeparator();
    menu.addAction("Add video track", [this]() {
        m_doc->apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Video).isEmpty(); });
    });
    menu.addAction("Add audio track", [this]() {
        m_doc->apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Audio).isEmpty(); });
    });
    menu.addSeparator();
    menu.addAction("Delete track", [this, t]() {
        m_doc->apply("Delete track", [&](Project &p) {
            const bool ok = TimelineOps::removeTrack(p, t.id);
            if (ok && t.kind == TrackKind::Caption) {
                bool used = false;
                for (const auto &other : p.tracks)
                    used = used || other.transcriptId == t.transcriptId;
                if (!used)
                    for (int i = 0; i < p.transcripts.size(); ++i)
                        if (p.transcripts[i].id == t.transcriptId)
                            p.transcripts.removeAt(i--);
            }
            return ok;
        });
    });
    menu.exec(global);
}

void TimelineWidget::showEmptyMenu(const QPoint &global, TimeUs at, int ti)
{
    QMenu menu(this);
    const QString trackId = ti >= 0 ? m_doc->project().tracks[ti].id : QString();
    menu.addAction("Add title here", [this, at, trackId]() {
        m_doc->apply("Add title", [&](Project &p) {
            TitleProps tp;
            tp.text = "Your title";
            return !TimelineOps::addTitleClip(p, trackId, at, 4 * kUsPerSec, tp).isEmpty();
        });
    });
    menu.addAction("Add shape here", [this, at, trackId]() {
        m_doc->apply("Add shape", [&](Project &p) {
            ShapeProps sp;
            sp.fill = QColor(0, 0, 0, 150);
            sp.y = 0.85;
            sp.w = 1.0;
            sp.h = 0.15;
            return !TimelineOps::addShapeClip(p, trackId, at, 4 * kUsPerSec, sp).isEmpty();
        });
    });
    menu.addSeparator();
    menu.addAction("Add video track", [this]() {
        m_doc->apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Video).isEmpty(); });
    });
    menu.addAction("Add audio track", [this]() {
        m_doc->apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Audio).isEmpty(); });
    });
    menu.exec(global);
}

// ------------------------------------------------------------------ drag & drop

static QString libraryLabel(const QString &id)
{
    if (id.startsWith("sticker:"))
        return id.mid(8);
    if (id.startsWith("text:"))
        return "Text";
    if (id.startsWith("title-"))
        return "Title";
    if (id.startsWith("shape-"))
        return "Shape";
    if (id.startsWith("fx:"))
        return "Effect layer: " + ImageFx::effectLabel(id.mid(3));
    if (id.startsWith("look:"))
        return "Filter layer";
    return id;
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *e)
{
    const QMimeData *md = e->mimeData();
    if (!m_doc->isOpen() || !(md->hasFormat(kMediaMime) || md->hasUrls() || md->hasFormat("application/x-shyftr-effect")))
        return;
    // what will land on the timeline, so we can draw it while dragging
    m_dropGhost.clear();
    m_dropLibraryId.clear();
    const Project &p = m_doc->project();
    if (md->hasFormat("application/x-shyftr-effect")) {
        m_dropLibraryId = QString::fromUtf8(md->data("application/x-shyftr-effect"));
        if (EffectsBrowser::dropKind(m_dropLibraryId) != "target")
            m_dropGhost << DropGhost{EffectsBrowser::dropDuration(m_dropLibraryId), libraryLabel(m_dropLibraryId), false,
                                     m_dropLibraryId.startsWith("shape-") ? Theme::clipShape()
                                     : (m_dropLibraryId.startsWith("fx:") || m_dropLibraryId.startsWith("look:")) ? QColor("#B0458C")
                                                                                                                  : Theme::clipTitle()};
    } else if (md->hasFormat(kMediaMime)) {
        for (const QString &id : QString::fromUtf8(md->data(kMediaMime)).split('\n', Qt::SkipEmptyParts))
            if (const MediaAsset *m = p.findMedia(id))
                m_dropGhost << DropGhost{m->kind == MediaKind::Image ? 5 * kUsPerSec : m->duration, m->name,
                                         m->kind == MediaKind::Audio,
                                         m->kind == MediaKind::Audio ? Theme::clipAudio()
                                         : m->kind == MediaKind::Image ? Theme::clipImage() : Theme::clipVideo()};
    } else {
        // files from Finder/Explorer: length unknown until imported, show a placeholder per file
        static const QStringList audioExt{"mp3", "wav", "m4a", "aac", "flac", "ogg", "opus", "aif", "aiff"};
        for (const QUrl &u : md->urls())
            if (u.isLocalFile()) {
                const QFileInfo fi(u.toLocalFile());
                const bool audio = audioExt.contains(fi.suffix().toLower());
                m_dropGhost << DropGhost{3 * kUsPerSec, fi.fileName(), audio, audio ? Theme::clipAudio() : Theme::clipVideo()};
            }
    }
    m_extDrag = true;
    m_autoScroll->start();
    e->acceptProposedAction();
    updateExternalDrag(e->position().toPoint(), md, e->modifiers());
}

void TimelineWidget::updateExternalDrag(const QPoint &pos, const QMimeData *mime, Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mime);
    m_lastDragPos = pos;
    m_lastMods = mods;
    m_dropTime = snapTime(std::max<TimeUs>(0, timeForX(pos.x())), {}, nullptr);
    m_dropRow = rowAtY(pos.y());
    m_dropLane = laneAtY(pos.y());
    m_dropInsert = (mods & (Qt::ControlModifier | Qt::MetaModifier)) && m_dropLane == 0;
    m_dropTargetClip.clear();
    m_insertAt = -1;
    const Project &p = m_doc->project();
    QString msg;
    if (!m_dropLibraryId.isEmpty()) {
        const QString kind = EffectsBrowser::dropKind(m_dropLibraryId);
        const Hit h = hitTest(pos);
        if (kind != "clip" && !h.clipId.isEmpty())
            m_dropTargetClip = h.clipId;
        if (m_dropLibraryId.startsWith("tr:"))
            msg = "Drop near a cut between two clips to add the transition";
        else if (!m_dropTargetClip.isEmpty())
            msg = "Release to apply to " + p.findClip(m_dropTargetClip)->name;
        else if (kind == "target")
            msg = "Drop this onto a clip";
        else
            msg = m_dropLane != 0 ? "Release to add on a new track" : "Release to add at " + formatClock(m_dropTime);
    }
    // will it land on a new track because the spot is taken?
    m_dropNewAbove = false;
    if (!m_dropInsert && m_dropLane == 0 && m_dropRow >= 0 && !m_dropGhost.isEmpty() && m_dropTargetClip.isEmpty()) {
        const Track &rt = p.tracks[m_rows[m_dropRow].trackIndex];
        TimeUs total = 0;
        for (const DropGhost &g : m_dropGhost)
            if ((rt.kind == TrackKind::Audio) == g.audio)
                total += g.duration;
        m_dropNewAbove = total > 0 && rt.kind != TrackKind::Caption && !isRangeFree(rt, m_dropTime, total);
    }
    if (m_dropLibraryId.isEmpty()) {
        if (m_dropInsert && m_dropRow >= 0)
            m_insertAt = insertPoint(p.tracks[m_rows[m_dropRow].trackIndex], m_dropTime);
        msg = m_dropLane != 0 ? "Release to add on a new track"
              : m_dropInsert ? "Insert at " + formatClock(m_insertAt) + " — clips after it move later"
                             : "Add at " + formatClock(m_dropTime) + (m_dropNewAbove ? " on a new track (that spot is taken)" : QString())
                                   + " · hold Ctrl/⌘ to insert and push clips aside";
    }
    emit statusMessage(msg);
    viewport()->update();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *e)
{
    updateExternalDrag(e->position().toPoint(), e->mimeData(), e->modifiers());
    if (!m_dropLibraryId.isEmpty() && EffectsBrowser::dropKind(m_dropLibraryId) == "target" && m_dropTargetClip.isEmpty()
        && !m_dropLibraryId.startsWith("tr:"))
        e->ignore(); // shows the "not here" cursor
    else
        e->acceptProposedAction();
}

void TimelineWidget::clearExternalDrag()
{
    m_extDrag = false;
    m_dropTime = -1;
    m_dropRow = -1;
    m_dropLane = 0;
    m_dropInsert = false;
    m_dropNewAbove = false;
    m_insertAt = -1;
    m_dropGhost.clear();
    m_dropTargetClip.clear();
    m_dropLibraryId.clear();
    if (m_drag == Drag::None)
        m_autoScroll->stop();
    viewport()->update();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent *e)
{
    Q_UNUSED(e);
    clearExternalDrag();
    emit statusMessage({});
}

void TimelineWidget::dropEvent(QDropEvent *e)
{
    updateExternalDrag(e->position().toPoint(), e->mimeData(), e->modifiers());
    const TimeUs at = m_dropTime >= 0 ? m_dropTime : 0;
    const Project &pr = m_doc->project();
    QString trackId;
    if (m_dropLane != 0)
        trackId = kNewTrack;
    else if (m_dropRow >= 0)
        trackId = pr.tracks[m_rows[m_dropRow].trackIndex].id;
    const bool insert = m_dropInsert;
    const QString targetClip = m_dropTargetClip;
    const int dropRow = m_dropRow;
    clearExternalDrag();
    const QMimeData *md = e->mimeData();
    if (md->hasFormat("application/x-shyftr-effect")) {
        const QString id = QString::fromUtf8(md->data("application/x-shyftr-effect"));
        QString target = targetClip;
        if (id.startsWith("tr:") && dropRow >= 0) {
            // a transition goes to the clip whose start (the cut) is nearest the drop point
            const Track &t = pr.tracks[m_rows[dropRow].trackIndex];
            TimeUs best = TimeUs(std::max(0.6, 40.0 / m_pxPerSec) * kUsPerSec);
            for (int i = 1; i < t.clips.size(); ++i)
                if (std::llabs(t.clips[i].start - at) < best) {
                    best = std::llabs(t.clips[i].start - at);
                    target = t.clips[i].id;
                }
        }
        // a clip made from the item goes on the video track it was dropped on (or a new one)
        QString clipTrack = trackId;
        if (const Track *t = pr.findTrack(trackId); t && t->kind != TrackKind::Video)
            clipTrack.clear();
        emit libraryItemDropped(id, target.isEmpty() ? QStringLiteral("-") : target, at, clipTrack);
        e->acceptProposedAction();
        return;
    }
    if (md->hasFormat(kMediaMime)) {
        const QStringList ids = QString::fromUtf8(md->data(kMediaMime)).split('\n', Qt::SkipEmptyParts);
        QStringList added;
        m_doc->apply(insert ? "Insert into timeline" : "Add to timeline", [&](Project &p) {
            added = TimelineOps::placeMedia(p, ids, trackId, at, insert ? PlaceMode::Insert : PlaceMode::Auto);
            return !added.isEmpty();
        });
        if (!added.isEmpty())
            m_doc->setSelectedClips(added);
        e->acceptProposedAction();
    } else if (md->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : md->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths, at, trackId, insert);
        e->acceptProposedAction();
    }
}

bool TimelineWidget::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::ToolTip && m_doc->isOpen()) {
        auto *he = static_cast<QHelpEvent *>(e);
        const Hit h = hitTest(he->pos());
        if (!h.clipId.isEmpty()) {
            const Clip *c = m_doc->project().findClip(h.clipId);
            QToolTip::showText(he->globalPos(), QString("%1\nStart %2 · Duration %3\nSource %4 – %5%6")
                                                    .arg(c->name, formatClock(c->start), formatClock(c->duration()),
                                                         formatClock(c->in), formatClock(c->out),
                                                         c->speed != 1 ? QString("\nSpeed %1×").arg(c->speed) : QString()));
            return true;
        }
        if (h.headerButton == "M")
            QToolTip::showText(he->globalPos(), "Mute track");
        else if (h.headerButton == "L")
            QToolTip::showText(he->globalPos(), "Lock track");
        else if (h.headerButton == "H")
            QToolTip::showText(he->globalPos(), "Hide track");
        else if (h.headerButton == "CC")
            QToolTip::showText(he->globalPos(), "Burn captions into exported video");
        else
            QToolTip::hideText();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

} // namespace ss
