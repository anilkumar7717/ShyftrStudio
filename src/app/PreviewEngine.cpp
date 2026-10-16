#include "PreviewEngine.h"
#include "../core/TimelineOps.h"
#include "../media/ColorMath.h"
#include "../media/ExportGraph.h"
#include "../core/Tools.h"

#include <QAudioOutput>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
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
    m_invalidateTimer.setSingleShot(true);
    m_invalidateTimer.setInterval(0);
    connect(&m_invalidateTimer, &QTimer::timeout, this, &PreviewEngine::doInvalidate);
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
    // Edits can arrive many times per frame (slider drags, on-video drags): mark the overlay
    // stale and do the work once, on the next pass of the event loop.
    m_overlayDirty = true;
    if (!m_invalidateTimer.isActive())
        m_invalidateTimer.start();
}

void PreviewEngine::ensureOverlay() const
{
    if (!m_overlayDirty || !m_doc->isOpen())
        return;
    m_overlay = OverlayScene::build(m_doc->project());
    m_overlayDirty = false;
}

void PreviewEngine::doInvalidate()
{
    if (!m_doc->isOpen())
        return;
    ensureOverlay();
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

QString PreviewEngine::audioSourceFor(const Clip &c, const MediaAsset &m)
{
    const QString chain = c.fx.audio.ffmpegChain(48000);
    if (chain.isEmpty() || m_doc->projectDir().isEmpty())
        return m.path;
    const QString key = QString::fromLatin1(QCryptographicHash::hash((m.path + "|" + chain).toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    if (m_audioFxReady.contains(key))
        return m_audioFxReady.value(key);
    if (m_audioFxFailed.contains(key))
        return m.path;
    const QString dir = m_doc->projectDir() + "/cache/audiofx";
    const QString out = dir + "/" + key + ".wav";
    if (QFileInfo::exists(out)) {
        m_audioFxReady.insert(key, out);
        return out;
    }
    if (!m_audioFxRunning.contains(key) && !Tools::ffmpeg().isEmpty()) {
        // Render the effect once in the background; until then the original sound plays.
        m_audioFxRunning.insert(key);
        QDir().mkpath(dir);
        auto *proc = new QProcess(this);
        const QString partial = dir + "/" + key + ".partial.wav";
        connect(proc, &QProcess::finished, this, [this, proc, key, partial, out](int code, QProcess::ExitStatus st) {
            proc->deleteLater();
            m_audioFxRunning.remove(key);
            if (code == 0 && st == QProcess::NormalExit && QFile::rename(partial, out)) {
                m_audioFxReady.insert(key, out);
                emit statusMessage("Voice effect ready for preview");
                syncSlots(m_doc->playhead(), m_playing);
            } else {
                QFile::remove(partial);
                m_audioFxFailed.insert(key); // do not retry on every tick
                emit statusMessage("The voice effect could not be prepared for preview (it is still applied on export)");
            }
        });
        proc->start(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-i", m.path, "-vn", "-af",
                                      "aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo," + chain, "-ar", "48000",
                                      "-c:a", "pcm_s16le", partial});
        emit statusMessage("Preparing the voice effect for preview…");
    }
    return m.path;
}

PreviewEngine::Slot *PreviewEngine::slotFor(const Clip &c, bool audio)
{
    const QString key = (audio ? "a:" : "v:") + c.id;
    const MediaAsset *m = m_doc->project().findMedia(c.mediaId);
    if (!m || m->missing)
        return nullptr;
    const QString path = audio ? audioSourceFor(c, *m) : m->path;
    if (Slot *s = m_slots.value(key)) {
        if (s->path == path) {
            s->lastUse = ++m_useCounter;
            return s;
        }
        // the source changed (e.g. the processed voice became ready): recreate the player
        m_slots.remove(key);
        s->player->stop();
        delete s->player;
        delete s->sink;
        delete s->audio;
        delete s;
    }
    // evict the least-recently-used slot of the same kind that this pass does not need
    int count = 0;
    Slot *lru = nullptr;
    for (Slot *s : std::as_const(m_slots))
        if (s->isAudio == audio) {
            ++count;
            if (!m_inUse.contains(s->key) && (!lru || s->lastUse < lru->lastUse))
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
        const QString slotKey = key;
        connect(s->sink, &QVideoSink::videoFrameChanged, this, [this, slotKey](const QVideoFrame &f) {
            Slot *s = m_slots.value(slotKey); // never touch a slot that was deleted meanwhile
            if (!s)
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
    s->path = path;
    s->player->setSource(QUrl::fromLocalFile(path));
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
    m_inUse.clear();
    for (const auto &need : Compositor::needsAt(p, t))
        m_inUse.insert("v:" + need.clipId);

    auto drive = [&](Slot *s, TimeUs src, double clipRate, bool isAudio, double volume) {
        keep.insert(s->key);
        const qint64 wantMs = src / kUsPerMs;
        if (playing) {
            const double rate = std::clamp(clipRate * m_rate, 0.25, 8.0);
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

    // video: every picture the compositor needs (including both sides of a transition)
    for (const auto &need : Compositor::needsAt(p, t)) {
        const Clip *c = p.findClip(need.clipId);
        const MediaAsset *m = p.findMedia(need.mediaId);
        if (!c || !m || m->kind != MediaKind::Video)
            continue;
        if (Slot *s = slotFor(*c, false))
            drive(s, need.sourceTime, Compositor::sourceRateAt(*c, t), false, 0);
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
                        const TimeUs half = c.fx.transitionIn.isSet() ? c.fx.transitionIn.duration / 2 : 0;
                        const qint64 wantMs = std::max<TimeUs>(0, c.timelineToSource(c.start - half)) / kUsPerMs;
                        if (s->player->playbackState() == QMediaPlayer::PlayingState)
                            s->player->pause();
                        if (std::llabs(s->player->position() - wantMs) > 40)
                            s->player->setPosition(wantMs);
                    }
                break;
            }
    }
    if (playing && m_rate > 0) {
        const auto active = TimelineOps::activeAudioClips(p, t);
        // music on "duck" tracks dips while other audio (voice) plays
        bool voiceActive = false;
        for (const Clip *c : active) {
            const Track *tr = nullptr;
            p.findClip(c->id, &tr);
            if (tr && !tr->duck && !c->audio.mute && !c->fx.speedCurve.isSet())
                voiceActive = true;
        }
        for (const Clip *c : active) {
            if (c->fx.speedCurve.isSet())
                continue; // a curve's audio is muted (as in export)
            const Track *tr = nullptr;
            p.findClip(c->id, &tr);
            double vol = std::pow(10.0, (c->audio.gainDb + (tr ? tr->volumeDb : 0)) / 20.0);
            if (c->audio.fadeIn > 0 && t < c->start + c->audio.fadeIn)
                vol *= double(t - c->start) / c->audio.fadeIn;
            if (c->audio.fadeOut > 0 && t > c->end() - c->audio.fadeOut)
                vol *= double(c->end() - t) / c->audio.fadeOut;
            if (tr && tr->duck && voiceActive)
                vol *= 0.25;
            if (Slot *s = slotFor(*c, true))
                drive(s, c->timelineToSource(t), c->speed, true, vol);
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

QImage PreviewEngine::frame(const Clip &clip, const MediaAsset &media, TimeUs)
{
    if (media.kind == MediaKind::Image)
        return stillImage(media);
    if (Slot *s = m_slots.value("v:" + clip.id))
        return s->frame;
    return {};
}

QImage PreviewEngine::renderStill(TimeUs t, QSize size, FrameSource &source) const
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    ensureOverlay();
    Compositor::render(canvas, m_doc->project(), m_overlay, t, source);
    return canvas;
}

void PreviewEngine::compose(TimeUs t)
{
    if (!m_doc->isOpen())
        return;
    m_lastComposeT = t;
    const QSize out = outputSize();
    QImage canvas(out, QImage::Format_ARGB32_Premultiplied);
    ensureOverlay();
    Compositor::render(canvas, m_doc->project(), m_overlay, t, *this);
    if (m_safeArea) {
        QPainter painter(&canvas);
        const QRectF safe = SafeArea::forFrame(out.width(), out.height());
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(safe);
        painter.drawLine(QPointF(out.width() / 2.0 - 8, out.height() / 2.0), QPointF(out.width() / 2.0 + 8, out.height() / 2.0));
        painter.drawLine(QPointF(out.width() / 2.0, out.height() / 2.0 - 8), QPointF(out.width() / 2.0, out.height() / 2.0 + 8));
    }
    m_frame = canvas;
    emit frameReady(m_frame);
}

} // namespace ss
