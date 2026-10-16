#include "Model.h"

#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ss {

Rational Rational::fromString(const QString &s, Rational fallback)
{
    const QStringList parts = s.split('/');
    bool ok1 = false, ok2 = true;
    int n = 0, d = 1;
    if (parts.size() == 2) {
        n = parts[0].trimmed().toInt(&ok1);
        d = parts[1].trimmed().toInt(&ok2);
    } else if (parts.size() == 1) {
        bool okd = false;
        double v = parts[0].toDouble(&okd);
        if (okd && v > 0)
            return fromDouble(v);
    }
    if (!ok1 || !ok2 || n <= 0 || d <= 0)
        return fallback;
    return {n, d};
}

Rational Rational::fromDouble(double fps)
{
    if (fps <= 0)
        return {30, 1};
    // Recognise NTSC rates exactly.
    const struct { double v; int n; int d; } known[] = {
        {23.976, 24000, 1001}, {29.97, 30000, 1001}, {59.94, 60000, 1001}, {47.952, 48000, 1001}, {119.88, 120000, 1001}};
    for (const auto &k : known)
        if (std::abs(fps - k.v) < 0.005)
            return {k.n, k.d};
    if (std::abs(fps - std::round(fps)) < 0.001)
        return {int(std::round(fps)), 1};
    return {int(std::round(fps * 1000)), 1000};
}

TimeUs framesToUs(qint64 frames, Rational fps)
{
    if (!fps.isValid())
        return 0;
    // t = frames * den / num seconds
    const long double us = (long double)frames * fps.den * kUsPerSec / fps.num;
    return TimeUs(std::llround(us));
}

qint64 usToFrameFloor(TimeUs t, Rational fps)
{
    if (!fps.isValid())
        return 0;
    // tolerance of 1us so that framesToUs(n) maps back to n
    const long double f = ((long double)t + 1) * fps.num / ((long double)fps.den * kUsPerSec);
    return qint64(std::floor(f));
}

qint64 usToFrameRound(TimeUs t, Rational fps)
{
    if (!fps.isValid())
        return 0;
    const long double f = (long double)t * fps.num / ((long double)fps.den * kUsPerSec);
    return qint64(std::llround(f));
}

TimeUs snapToFrame(TimeUs t, Rational fps)
{
    return framesToUs(usToFrameRound(t, fps), fps);
}

TimeUs frameDurationUs(Rational fps)
{
    return framesToUs(1, fps);
}

QString formatTimecode(TimeUs t, Rational fps)
{
    const bool neg = t < 0;
    if (neg)
        t = -t;
    const qint64 frame = usToFrameFloor(t, fps);
    const int nominal = std::max(1, int(std::lround(fps.toDouble())));
    const qint64 totalSec = t / kUsPerSec;
    const int ff = int(frame - usToFrameFloor(totalSec * kUsPerSec, fps));
    return QString("%1%2:%3:%4:%5")
        .arg(neg ? "-" : "")
        .arg(totalSec / 3600, 2, 10, QChar('0'))
        .arg((totalSec / 60) % 60, 2, 10, QChar('0'))
        .arg(totalSec % 60, 2, 10, QChar('0'))
        .arg(std::clamp(ff, 0, nominal - 1), 2, 10, QChar('0'));
}

QString formatClock(TimeUs t, bool withMillis)
{
    const bool neg = t < 0;
    if (neg)
        t = -t;
    const qint64 ms = t / kUsPerMs;
    QString s = QString("%1%2:%3:%4")
                    .arg(neg ? "-" : "")
                    .arg(ms / 3600000, 2, 10, QChar('0'))
                    .arg((ms / 60000) % 60, 2, 10, QChar('0'))
                    .arg((ms / 1000) % 60, 2, 10, QChar('0'));
    if (withMillis)
        s += QString(".%1").arg(ms % 1000, 3, 10, QChar('0'));
    return s;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(13).remove('-');
}

QString toString(MediaKind k)
{
    switch (k) {
    case MediaKind::Video: return "video";
    case MediaKind::Audio: return "audio";
    case MediaKind::Image: return "image";
    }
    return "video";
}
QString toString(ClipType k)
{
    switch (k) {
    case ClipType::Media: return "media";
    case ClipType::Title: return "title";
    case ClipType::Shape: return "shape";
    case ClipType::Effect: return "effect";
    }
    return "media";
}
QString toString(TrackKind k)
{
    switch (k) {
    case TrackKind::Video: return "video";
    case TrackKind::Audio: return "audio";
    case TrackKind::Caption: return "caption";
    }
    return "video";
}
MediaKind mediaKindFromString(const QString &s)
{
    if (s == "audio") return MediaKind::Audio;
    if (s == "image") return MediaKind::Image;
    return MediaKind::Video;
}
ClipType clipTypeFromString(const QString &s)
{
    if (s == "title") return ClipType::Title;
    if (s == "shape") return ClipType::Shape;
    if (s == "effect") return ClipType::Effect;
    return ClipType::Media;
}
TrackKind trackKindFromString(const QString &s)
{
    if (s == "audio") return TrackKind::Audio;
    if (s == "caption") return TrackKind::Caption;
    return TrackKind::Video;
}

bool Transform::isIdentity() const
{
    return cropL == 0 && cropT == 0 && cropR == 0 && cropB == 0 && rotation == 0 && !flipH && !flipV
        && fit == "fit" && scale == 1.0 && posX == 0 && posY == 0 && opacity == 1.0;
}

bool ColorAdjust::isIdentity() const
{
    return exposure == 0 && contrast == 1 && saturation == 1 && temperature == 0 && tint == 0;
}

TimeUs Clip::duration() const
{
    const double s = speed > 0 ? speed : 1.0;
    return TimeUs(std::llround(double(out - in) / s));
}

TimeUs Clip::sourceToTimeline(TimeUs src) const
{
    const double s = speed > 0 ? speed : 1.0;
    if (fx.speedCurve.isSet() && out > in && src >= in && src <= out) {
        const TimeUs d = duration();
        return start + TimeUs(std::llround(fx.speedCurve.timelineFraction(double(src - in) / double(out - in)) * d));
    }
    return start + TimeUs(std::llround(double(src - in) / s));
}

TimeUs Clip::timelineToSource(TimeUs t) const
{
    const double s = speed > 0 ? speed : 1.0;
    if (fx.speedCurve.isSet() && t >= start && t <= end()) {
        const TimeUs d = std::max<TimeUs>(1, duration());
        return in + TimeUs(std::llround(fx.speedCurve.sourceFraction(double(t - start) / double(d)) * double(out - in)));
    }
    return in + TimeUs(std::llround(double(t - start) * s));
}

AnimatedValues Clip::baseValues() const
{
    AnimatedValues v;
    v.scale = transform.scale;
    v.rotation = transform.rotation;
    v.opacity = transform.opacity;
    if (type == ClipType::Title) {
        v.x = title.x;
        v.y = title.y;
    } else if (type == ClipType::Shape) {
        v.x = shape.x;
        v.y = shape.y;
    } else {
        v.x = transform.posX;
        v.y = transform.posY;
    }
    return v;
}

void Clip::setBaseValues(const AnimatedValues &v)
{
    transform.scale = v.scale;
    transform.rotation = v.rotation;
    transform.opacity = std::clamp(v.opacity, 0.0, 1.0);
    if (type == ClipType::Title) {
        title.x = v.x;
        title.y = v.y;
    } else if (type == ClipType::Shape) {
        shape.x = v.x;
        shape.y = v.y;
    } else {
        transform.posX = v.x;
        transform.posY = v.y;
    }
}

Clip Clip::animatedAt(TimeUs t) const
{
    if (fx.keyframes.isEmpty())
        return *this;
    Clip c = *this;
    c.setBaseValues(Keyframes::evaluate(fx.keyframes, t - start));
    return c;
}

void Track::sortClips()
{
    std::stable_sort(clips.begin(), clips.end(), [](const Clip &a, const Clip &b) { return a.start < b.start; });
}

int Track::indexOfClip(const QString &clipId) const
{
    for (int i = 0; i < clips.size(); ++i)
        if (clips[i].id == clipId)
            return i;
    return -1;
}

QString Transcript::cueText(int cueIndex) const
{
    if (cueIndex < 0 || cueIndex >= cues.size())
        return {};
    const Cue &c = cues[cueIndex];
    QStringList parts;
    for (int i = c.first; i <= c.last && i < words.size(); ++i)
        parts << words[i].text;
    return parts.join(' ');
}

TimeUs Transcript::cueStart(int cueIndex) const
{
    if (cueIndex < 0 || cueIndex >= cues.size() || cues[cueIndex].first >= words.size())
        return 0;
    return words[cues[cueIndex].first].start;
}

TimeUs Transcript::cueEnd(int cueIndex) const
{
    if (cueIndex < 0 || cueIndex >= cues.size() || cues[cueIndex].last >= words.size())
        return 0;
    return words[cues[cueIndex].last].end;
}

int Transcript::cueAt(TimeUs t) const
{
    for (int i = 0; i < cues.size(); ++i)
        if (t >= cueStart(i) && t < cueEnd(i))
            return i;
    return -1;
}

QString CanvasSettings::aspectLabel() const
{
    if (width <= 0 || height <= 0)
        return "?";
    const double r = double(width) / height;
    if (std::abs(r - 16.0 / 9) < 0.02) return "16:9";
    if (std::abs(r - 9.0 / 16) < 0.02) return "9:16";
    if (std::abs(r - 1.0) < 0.02) return "1:1";
    if (std::abs(r - 4.0 / 5) < 0.02) return "4:5";
    if (std::abs(r - 4.0 / 3) < 0.02) return "4:3";
    return QString::number(r, 'f', 2) + ":1";
}

// The const finders only read, so a project that shares its data with an undo snapshot
// (Qt implicit sharing) is never copied just by looking things up. The non-const finders
// locate the item read-only first and then detach only the containers on its path.
const MediaAsset *Project::findMedia(const QString &id) const
{
    for (const auto &m : media)
        if (m.id == id)
            return &m;
    return nullptr;
}
MediaAsset *Project::findMedia(const QString &id)
{
    for (int i = 0; i < media.size(); ++i)
        if (std::as_const(media)[i].id == id)
            return &media[i];
    return nullptr;
}
const Track *Project::findTrack(const QString &id) const
{
    for (const auto &t : tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}
Track *Project::findTrack(const QString &id)
{
    for (int i = 0; i < tracks.size(); ++i)
        if (std::as_const(tracks)[i].id == id)
            return &tracks[i];
    return nullptr;
}
const Clip *Project::findClip(const QString &id, const Track **track) const
{
    for (const auto &t : tracks)
        for (const auto &c : t.clips)
            if (c.id == id) {
                if (track)
                    *track = &t;
                return &c;
            }
    if (track)
        *track = nullptr;
    return nullptr;
}
Clip *Project::findClip(const QString &id, Track **track)
{
    const auto &ct = std::as_const(tracks);
    for (int ti = 0; ti < ct.size(); ++ti)
        for (int ci = 0; ci < ct[ti].clips.size(); ++ci)
            if (ct[ti].clips[ci].id == id) {
                Track &t = tracks[ti];
                if (track)
                    *track = &t;
                return &t.clips[ci];
            }
    return nullptr;
}
const Transcript *Project::findTranscript(const QString &id) const
{
    for (const auto &t : transcripts)
        if (t.id == id)
            return &t;
    return nullptr;
}
Transcript *Project::findTranscript(const QString &id)
{
    for (int i = 0; i < transcripts.size(); ++i)
        if (std::as_const(transcripts)[i].id == id)
            return &transcripts[i];
    return nullptr;
}
const Speaker *Project::findSpeaker(int id) const
{
    for (const auto &s : speakers)
        if (s.id == id)
            return &s;
    return nullptr;
}

QVector<QPair<int, int>> Project::linkedClipIndices(const QString &clipId) const
{
    QVector<QPair<int, int>> out;
    const Clip *c = findClip(clipId);
    if (!c)
        return out;
    const QString link = c->linkId;
    for (int ti = 0; ti < tracks.size(); ++ti)
        for (int ci = 0; ci < tracks[ti].clips.size(); ++ci) {
            const Clip &cl = tracks[ti].clips[ci];
            if (link.isEmpty() ? cl.id == clipId : cl.linkId == link)
                out.append({ti, ci});
        }
    return out;
}

QVector<Clip *> Project::linkedClips(const QString &clipId)
{
    QVector<Clip *> out;
    for (const auto &[ti, ci] : linkedClipIndices(clipId))
        out << &tracks[ti].clips[ci];
    return out;
}

QVector<const Clip *> Project::linkedClips(const QString &clipId) const
{
    QVector<const Clip *> out;
    for (const auto &[ti, ci] : linkedClipIndices(clipId))
        out << &tracks[ti].clips[ci];
    return out;
}

QString TitleProps::displayText() const
{
    QString t = text;
    t.remove('*');
    if (textCase == "upper")
        t = t.toUpper();
    else if (textCase == "lower")
        t = t.toLower();
    else if (textCase == "title") {
        bool start = true;
        for (QChar &ch : t) {
            if (ch.isLetter()) {
                ch = start ? ch.toUpper() : ch.toLower();
                start = false;
            } else {
                start = ch.isSpace() || ch == '-';
            }
        }
    }
    return t;
}

TimeUs Project::duration() const
{
    TimeUs d = 0;
    for (const auto &t : tracks)
        for (const auto &c : t.clips)
            d = std::max(d, c.end());
    return d;
}

int Project::trackIndex(const QString &id) const
{
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return i;
    return -1;
}

QVector<int> Project::trackIndicesOfKind(TrackKind k) const
{
    QVector<int> out;
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks[i].kind == k)
            out << i;
    return out;
}

Project makeEmptyProject(const QString &name, const CanvasSettings &canvas)
{
    Project p;
    p.id = newId();
    p.name = name;
    p.created = QDateTime::currentDateTimeUtc();
    p.modified = p.created;
    p.canvas = canvas;
    Track v1;
    v1.id = newId();
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    Track v2 = v1;
    v2.id = newId();
    v2.name = "V2";
    Track a1;
    a1.id = newId();
    a1.kind = TrackKind::Audio;
    a1.name = "A1";
    Track a2 = a1;
    a2.id = newId();
    a2.name = "A2";
    p.tracks = {v1, v2, a1, a2};
    p.speakers = {{0, "Speaker 1", QColor("#FFD43B")}, {1, "Speaker 2", QColor("#4DABF7")}};
    return p;
}

} // namespace ss
