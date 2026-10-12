#include "PreviewEngine.h"
#include "../core/TimelineOps.h"
#include "../media/ColorMath.h"
#include "../media/ExportGraph.h"

#include <QAudioOutput>
#include <QImageReader>
#include <QMediaPlayer>
#include <QPainter>
#include <QSet>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ss {

static constexpr int kMaxVideoSlots = 4;
static constexpr int kMaxAudioSlots = 8;

PreviewEngine::PreviewEngine(Document *doc, QObject *parent) : QObject(parent), m_doc(doc)
{
    m_timer.setInterval(15);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PreviewEngine::tick);
    m_composeTimer.setSingleShot(true);
    m_composeTimer.setInterval(0);
    connect(&m_composeTimer, &QTimer::timeout, this, [this]() { compose(m_doc->playhead()); });
    connect(doc, &Document::projectChanged, this, &PreviewEngine::invalidate);
    connect(doc, &Document::projectReset, this, [this]() {
        pause();
        for (Slot *s : std::as_const(m_slots)) {
            delete s->player;
            delete s->sink;
            delete s->audio;
            delete s;
        }
        m_slots.clear();
        m_stills.clear();
        invalidate();
    });
}

PreviewEngine::~PreviewEngine()
{
    for (Slot *s : std::as_const(m_slots)) {
        if (s->player)
            s->player->stop();
        delete s->player;
        delete s->sink;
        delete s->audio;
        delete s;
    }
}

void PreviewEngine::setMaxPreviewWidth(int w)
{
    m_maxWidth = std::clamp(w, 320, 3840);
    scheduleCompose();
}

QSize PreviewEngine::outputSize() const
{
    const CanvasSettings &c = m_doc->project().canvas;
    if (c.width <= 0 || c.height <= 0)
        return {640, 360};
    const int maxH = m_maxWidth; // keep vertical canvases sharp too
    double k = std::min(1.0, std::min(double(m_maxWidth) / c.width, double(maxH) / c.height));
    return QSize(std::max(2, int(c.width * k)), std::max(2, int(c.height * k)));
}

void PreviewEngine::setShowSafeArea(bool on)
{
    m_safeArea = on;
    scheduleCompose();
}

void PreviewEngine::invalidate()
{
    if (!m_doc->isOpen())
        return;
    m_overlay = OverlayScene::build(m_doc->project());
    syncSlots(m_doc->playhead(), m_playing);
    scheduleCompose();
}

void PreviewEngine::play()
{
    if (m_playing || !m_doc->isOpen())
        return;
    const TimeUs dur = m_doc->project().duration();
    if (dur <= 0)
        return;
    if (m_doc->playhead() >= dur - frameDurationUs(m_doc->project().canvas.fps))
        m_doc->setPlayhead(0);
    m_playing = true;
    if (m_rate <= 0)
        m_rate = 1.0;
    m_clockBase = m_doc->playhead();
    m_clock.start();
    syncSlots(m_clockBase, true);
    m_timer.start();
    emit playStateChanged(true);
}

void PreviewEngine::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.stop();
    m_rate = 1.0;
    for (Slot *s : std::as_const(m_slots))
        if (s->player)
            s->player->pause();
    syncSlots(m_doc->playhead(), false);
    emit playStateChanged(false);
}

void PreviewEngine::togglePlay()
{
    m_playing ? pause() : play();
}

void PreviewEngine::shuttle(int direction)
{
    if (direction == 0) {
        pause();
        return;
    }
    if (direction > 0) {
        if (!m_playing) {
            m_rate = 1.0;
            play();
        } else {
            m_rate = m_rate < 0 ? 1.0 : std::min(8.0, m_rate * 2);
            m_clockBase = m_doc->playhead();
            m_clock.restart();
            syncSlots(m_clockBase, true);
        }
    } else {
        // Reverse: decoders cannot play backwards, so the clock steps back and frames are sought.
        if (!m_playing)
            play();
        m_rate = m_rate > 0 ? -1.0 : std::max(-8.0, m_rate * 2);
        m_clockBase = m_doc->playhead();
        m_clock.restart();
        for (Slot *s : std::as_const(m_slots))
            if (s->player)
                s->player->pause();
    }
    emit statusMessage(QString("Playback speed %1×").arg(m_rate));
}

void PreviewEngine::seek(TimeUs t)
{
    const TimeUs dur = m_doc->project().duration();
    t = std::clamp<TimeUs>(t, 0, std::max<TimeUs>(0, dur));
    m_doc->setPlayhead(t);
    if (m_playing) {
        m_clockBase = t;
        m_clock.restart();
    }
    syncSlots(t, m_playing && m_rate > 0);
    scheduleCompose();
}

void PreviewEngine::stepFrames(int n)
{
    if (m_playing)
        pause();
    const Rational fps = m_doc->project().canvas.fps;
    const qint64 f = usToFrameRound(m_doc->playhead(), fps) + n;
    seek(framesToUs(std::max<qint64>(0, f), fps));
}

void PreviewEngine::tick()
{
    if (!m_playing)
        return;
    const Project &p = m_doc->project();
    TimeUs t = m_clockBase + TimeUs(m_clock.nsecsElapsed() / 1000.0 * m_rate);
    TimeUs end = p.duration();
    TimeUs start = 0;
    if (m_loop && p.inPoint >= 0 && p.outPoint > p.inPoint) {
        start = p.inPoint;
        end = p.outPoint;
    }
    if (t >= end) {
        if (m_loop) {
            t = start;
            m_clockBase = start;
            m_clock.restart();
        } else {
            m_doc->setPlayhead(end);
            pause();
            scheduleCompose();
            return;
        }
    }
    if (t <= 0 && m_rate < 0) {
        m_doc->setPlayhead(0);
        pause();
        return;
    }
    m_doc->setPlayhead(t);
    syncSlots(t, m_rate > 0);
    if (std::llabs(t - m_lastComposeT) >= 30 * kUsPerMs || m_rate < 0)
        compose(t);
}

PreviewEngine::Slot *PreviewEngine::slotFor(const Clip &c, bool audio)
{
    const QString key = (audio ? "a:" : "v:") + c.id;
    if (Slot *s = m_slots.value(key)) {
        s->lastUse = ++m_useCounter;
        return s;
    }
    const MediaAsset *m = m_doc->project().findMedia(c.mediaId);
    if (!m || m->missing)
        return nullptr;
    // evict least-recently-used slots of the same kind
    int count = 0;
    Slot *lru = nullptr;
    for (Slot *s : std::as_const(m_slots))
        if (s->isAudio == audio) {
            ++count;
            if (!lru || s->lastUse < lru->lastUse)
                lru = s;
        }
    if (count >= (audio ? kMaxAudioSlots : kMaxVideoSlots) && lru) {
        m_slots.remove(lru->key);
        lru->player->stop();
        delete lru->player;
        delete lru->sink;
        delete lru->audio;
        delete lru;
    }
    auto *s = new Slot;
    s->key = key;
    s->mediaId = c.mediaId;
    s->isAudio = audio;
    s->lastUse = ++m_useCounter;
    s->player = new QMediaPlayer;
    if (audio) {
        s->audio = new QAudioOutput;
        s->player->setAudioOutput(s->audio);
    } else {
        s->sink = new QVideoSink;
        s->player->setVideoSink(s->sink);
        connect(s->sink, &QVideoSink::videoFrameChanged, this, [this, s](const QVideoFrame &f) {
            if (!m_slots.contains(s->key))
                return;
            QImage img = f.toImage();
            if (img.isNull())
                return;
            // keep preview cheap: downscale large frames once
            const QSize out = outputSize();
            if (img.width() > out.width() * 2 && img.height() > out.height() * 2)
                img = img.scaled(img.size() / 2, Qt::KeepAspectRatio, Qt::FastTransformation);
            s->frame = img;
            s->ready = true;
            scheduleCompose();
        });
    }
    const QString mediaName = m->name;
    connect(s->player, &QMediaPlayer::errorOccurred, this, [this, mediaName](QMediaPlayer::Error, const QString &msg) {
        emit statusMessage(QString("Preview could not decode %1: %2").arg(mediaName, msg));
    });
    s->player->setSource(QUrl::fromLocalFile(m->path));
    m_slots.insert(key, s);
    return s;
}

void PreviewEngine::releaseUnused(const QSet<QString> &keep)
{
    for (Slot *s : std::as_const(m_slots))
        if (!keep.contains(s->key) && s->player && s->player->playbackState() == QMediaPlayer::PlayingState)
            s->player->pause();
}

void PreviewEngine::syncSlots(TimeUs t, bool playing)
{
    if (!m_doc->isOpen())
        return;
    const Project &p = m_doc->project();
    QSet<QString> keep;
    const TimeUs tolerance = playing ? 150 * kUsPerMs : 0;

    auto drive = [&](Slot *s, const Clip &c, bool isAudio, double volume) {
        keep.insert(s->key);
        const TimeUs src = c.timelineToSource(t);
        const qint64 wantMs = src / kUsPerMs;
        if (playing) {
            const double rate = std::clamp(c.speed * m_rate, 0.25, 8.0);
            if (std::abs(s->player->playbackRate() - rate) > 1e-3)
                s->player->setPlaybackRate(rate);
            if (std::llabs(s->player->position() - wantMs) * kUsPerMs > tolerance)
                s->player->setPosition(wantMs);
            if (s->player->playbackState() != QMediaPlayer::PlayingState)
                s->player->play();
        } else {
            if (s->player->playbackState() == QMediaPlayer::PlayingState)
                s->player->pause();
            if (s->player->position() != wantMs)
                s->player->setPosition(wantMs);
        }
        if (isAudio && s->audio)
            s->audio->setVolume(float(std::clamp(volume, 0.0, 1.0)));
    };

    for (const Clip *c : TimelineOps::activeVideoClips(p, t)) {
        const MediaAsset *m = p.findMedia(c->mediaId);
        if (!m || m->kind != MediaKind::Video)
            continue;
        if (Slot *s = slotFor(*c, false))
            drive(s, *c, false, 0);
    }
    // pre-roll the next video clip on each track so cuts are seamless
    for (const auto &tr : p.tracks) {
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        for (const auto &c : tr.clips)
            if (c.start > t && c.start - t < kUsPerSec && c.type == ClipType::Media) {
                const MediaAsset *m = p.findMedia(c.mediaId);
                if (m && m->kind == MediaKind::Video)
                    if (Slot *s = slotFor(c, false)) {
                        keep.insert(s->key);
                        const qint64 wantMs = c.in / kUsPerMs;
                        if (s->player->playbackState() == QMediaPlayer::PlayingState)
                            s->player->pause();
                        if (std::llabs(s->player->position() - wantMs) > 40)
                            s->player->setPosition(wantMs);
                    }
                break;
            }
    }
    if (playing && m_rate > 0) {
        for (const Clip *c : TimelineOps::activeAudioClips(p, t)) {
            const Track *tr = nullptr;
            p.findClip(c->id, &tr);
            double vol = std::pow(10.0, (c->audio.gainDb + (tr ? tr->volumeDb : 0)) / 20.0);
            if (c->audio.fadeIn > 0 && t < c->start + c->audio.fadeIn)
                vol *= double(t - c->start) / c->audio.fadeIn;
            if (c->audio.fadeOut > 0 && t > c->end() - c->audio.fadeOut)
                vol *= double(c->end() - t) / c->audio.fadeOut;
            if (Slot *s = slotFor(*c, true))
                drive(s, *c, true, vol);
        }
    }
    releaseUnused(keep);
}

QImage PreviewEngine::stillImage(const MediaAsset &m)
{
    auto it = m_stills.find(m.id);
    if (it != m_stills.end())
        return it.value();
    QImageReader r(m.path);
    r.setAutoTransform(true);
    QImage img = r.read();
    const QSize out = outputSize();
    if (img.width() > out.width() * 2)
        img = img.scaledToWidth(out.width() * 2, Qt::SmoothTransformation);
    m_stills.insert(m.id, img);
    return img;
}

void PreviewEngine::scheduleCompose()
{
    if (!m_composeTimer.isActive())
        m_composeTimer.start();
}

void PreviewEngine::drawClip(QPainter &p, const Clip &c, const MediaAsset &m, const QImage &srcIn, TimeUs t)
{
    const CanvasSettings &cv = m_doc->project().canvas;
    const QSize out = outputSize();
    const double k = double(out.width()) / cv.width;
    QImage src = srcIn;
    // Decoders may hand over frames in coded orientation; rotate to display orientation.
    if (m.width != m.height && ((src.width() > src.height()) != (m.width > m.height))) {
        QTransform rot;
        rot.rotate(m.rotation == 270 ? -90 : 90);
        src = src.transformed(rot);
    }
    const ClipGeometry g = computeGeometry(m.width, m.height, c.transform, cv.width, cv.height);
    const double sx = double(src.width()) / std::max(1, m.width);
    const double sy = double(src.height()) / std::max(1, m.height);
    const QRect crop(int(g.crop.x() * sx), int(g.crop.y() * sy), int(g.crop.width() * sx), int(g.crop.height() * sy));
    QImage part = src.copy(crop);
    if (!c.color.isIdentity()) {
        // colour on a preview-sized copy (same maths as export)
        const QSize target(std::max(2, int(g.crop.width() * g.scaleX * k)), std::max(2, int(g.crop.height() * g.scaleY * k)));
        if (part.width() > target.width() * 1.5)
            part = part.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        part = part.convertToFormat(QImage::Format_ARGB32);
        ColorMath::apply(part, c.color);
    }
    const double opacity = GraphicsRenderer::clipOpacity(c, t);
    if (opacity <= 0.001)
        return;
    double drawW = g.crop.width() * g.scaleX * k;
    double drawH = g.crop.height() * g.scaleY * k;
    if (g.quarterTurns % 2 == 1) {
        drawW = g.crop.width() * g.scaleY * k;
        drawH = g.crop.height() * g.scaleX * k;
    }
    const QPointF center((g.x + g.outW / 2) * k, (g.y + g.outH / 2) * k);
    p.save();
    p.setOpacity(opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(center);
    p.rotate(g.quarterTurns * 90 + g.freeAngle);
    p.scale(c.transform.flipH ? -1 : 1, c.transform.flipV ? -1 : 1);
    p.drawImage(QRectF(-drawW / 2, -drawH / 2, drawW, drawH), part);
    p.restore();
}

void PreviewEngine::compose(TimeUs t)
{
    if (!m_doc->isOpen())
        return;
    m_lastComposeT = t;
    const Project &p = m_doc->project();
    const QSize out = outputSize();
    QImage canvas(out, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(p.canvas.background);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const Clip *c : TimelineOps::activeVideoClips(p, t)) {
        const MediaAsset *m = p.findMedia(c->mediaId);
        if (!m)
            continue;
        if (m->missing) {
            painter.fillRect(canvas.rect(), QColor(60, 20, 20));
            painter.setPen(Qt::white);
            painter.drawText(canvas.rect(), Qt::AlignCenter, "Media offline: " + m->name + "\nUse File → Relink Media");
            continue;
        }
        QImage src;
        if (m->kind == MediaKind::Image) {
            src = stillImage(*m);
        } else if (Slot *s = m_slots.value("v:" + c->id)) {
            src = s->frame;
        }
        if (src.isNull())
            continue;
        drawClip(painter, *c, *m, src, t);
    }
    painter.save();
    painter.scale(1.0, 1.0);
    m_overlay.render(painter, out, t);
    painter.restore();
    if (m_safeArea) {
        const QRectF safe = SafeArea::forFrame(out.width(), out.height());
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(safe);
        painter.drawLine(QPointF(out.width() / 2.0 - 8, out.height() / 2.0), QPointF(out.width() / 2.0 + 8, out.height() / 2.0));
        painter.drawLine(QPointF(out.width() / 2.0, out.height() / 2.0 - 8), QPointF(out.width() / 2.0, out.height() / 2.0 + 8));
    }
    painter.end();
    m_frame = canvas;
    emit frameReady(m_frame);
}

} // namespace ss
