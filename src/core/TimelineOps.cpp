#include "TimelineOps.h"

#include <QRegularExpression>

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace ss {
namespace TimelineOps {

namespace {
constexpr TimeUs kMinClipDuration = 10 * kUsPerMs;

QString uniqueTrackName(const Project &p, TrackKind kind)
{
    const QString prefix = kind == TrackKind::Video ? "V" : kind == TrackKind::Audio ? "A" : "C";
    int n = 1;
    QSet<QString> names;
    for (const auto &t : p.tracks)
        names.insert(t.name);
    while (names.contains(prefix + QString::number(n)))
        ++n;
    return prefix + QString::number(n);
}

bool overlaps(TimeUs a0, TimeUs a1, TimeUs b0, TimeUs b1)
{
    return a0 < b1 && b0 < a1;
}

QStringList expandLinked(Project &p, const QStringList &ids, bool includeLinked)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &id : ids) {
        if (includeLinked) {
            for (Clip *c : p.linkedClips(id))
                if (!seen.contains(c->id)) {
                    seen.insert(c->id);
                    out << c->id;
                }
        } else if (p.findClip(id) && !seen.contains(id)) {
            seen.insert(id);
            out << id;
        }
    }
    return out;
}

Track *firstTrackWithSpace(Project &p, TrackKind kind, TimeUs start, TimeUs dur)
{
    for (auto &t : p.tracks)
        if (t.kind == kind && !t.locked && isRangeFree(t, start, dur))
            return &t;
    return nullptr;
}
} // namespace

QString addTrack(Project &p, TrackKind kind, const QString &name)
{
    Track t;
    t.id = newId();
    t.kind = kind;
    t.name = name.isEmpty() ? uniqueTrackName(p, kind) : name;
    // Insert next to the last track of the same kind to keep groups together.
    int insertAt = p.tracks.size();
    for (int i = p.tracks.size() - 1; i >= 0; --i)
        if (p.tracks[i].kind == kind) {
            insertAt = i + 1;
            break;
        }
    p.tracks.insert(insertAt, t);
    return t.id;
}

bool removeTrack(Project &p, const QString &trackId)
{
    const int idx = p.trackIndex(trackId);
    if (idx < 0)
        return false;
    // Drop linkIds that would now point at a single clip.
    QStringList links;
    for (const auto &c : p.tracks[idx].clips)
        if (!c.linkId.isEmpty())
            links << c.linkId;
    p.tracks.removeAt(idx);
    for (const QString &l : links) {
        int count = 0;
        for (auto &t : p.tracks)
            for (auto &c : t.clips)
                if (c.linkId == l)
                    ++count;
        if (count <= 1)
            for (auto &t : p.tracks)
                for (auto &c : t.clips)
                    if (c.linkId == l)
                        c.linkId.clear();
    }
    return true;
}

bool moveTrack(Project &p, const QString &trackId, int delta)
{
    const int idx = p.trackIndex(trackId);
    const int to = idx + delta;
    if (idx < 0 || to < 0 || to >= p.tracks.size() || p.tracks[to].kind != p.tracks[idx].kind)
        return false;
    p.tracks.move(idx, to);
    return true;
}

QString addTrackAt(Project &p, TrackKind kind, int index, const QString &name)
{
    Track t;
    t.id = newId();
    t.kind = kind;
    t.name = name.isEmpty() ? uniqueTrackName(p, kind) : name;
    // keep tracks of one kind together: clamp into (or right next to) that kind's block
    int first = -1, last = -1;
    for (int i = 0; i < p.tracks.size(); ++i)
        if (p.tracks[i].kind == kind) {
            if (first < 0)
                first = i;
            last = i;
        }
    if (first < 0)
        return addTrack(p, kind, t.name);
    index = std::clamp(index, first, last + 1);
    p.tracks.insert(index, t);
    return t.id;
}

QString addTrackNextTo(Project &p, const QString &nextTo, TrackKind kind)
{
    const int idx = p.trackIndex(nextTo);
    if (idx < 0 || p.tracks[idx].kind != kind)
        return addTrack(p, kind);
    // video: a higher index is drawn above; audio/captions: a higher index is drawn below
    return addTrackAt(p, kind, idx + 1);
}

bool moveTrackTo(Project &p, const QString &trackId, int newIndex)
{
    const int idx = p.trackIndex(trackId);
    if (idx < 0 || newIndex < 0 || newIndex >= p.tracks.size() || newIndex == idx)
        return false;
    if (p.tracks[newIndex].kind != p.tracks[idx].kind)
        return false;
    p.tracks.move(idx, newIndex);
    return true;
}

bool isRangeFree(const Track &t, TimeUs start, TimeUs dur, const QStringList &ignore)
{
    if (start < 0)
        return false;
    for (const auto &c : t.clips) {
        if (ignore.contains(c.id))
            continue;
        if (overlaps(start, start + dur, c.start, c.end()))
            return false;
    }
    return true;
}

TimeUs nextFreeStart(const Track &t, TimeUs start, TimeUs dur)
{
    TimeUs s = std::max<TimeUs>(0, start);
    bool moved = true;
    while (moved) {
        moved = false;
        for (const auto &c : t.clips)
            if (overlaps(s, s + dur, c.start, c.end())) {
                s = c.end();
                moved = true;
            }
    }
    return s;
}

TimeUs mediaSourceLength(const Project &p, const Clip &c)
{
    if (c.type != ClipType::Media)
        return -1;
    const MediaAsset *m = p.findMedia(c.mediaId);
    if (!m || m->kind == MediaKind::Image)
        return -1;
    return m->duration;
}

QString addMediaClip(Project &p, const QString &mediaId, const QString &trackId, TimeUs start, bool withLinkedAudio,
                     TimeUs imageDuration)
{
    const MediaAsset *m = p.findMedia(mediaId);
    if (!m)
        return {};
    const TimeUs dur = m->kind == MediaKind::Image ? imageDuration : m->duration;
    if (dur <= 0)
        return {};
    start = snapToFrame(std::max<TimeUs>(0, start), p.canvas.fps);

    const TrackKind primaryKind = m->kind == MediaKind::Audio ? TrackKind::Audio : TrackKind::Video;
    Track *primary = (trackId.isEmpty() || trackId == kNewTrack) ? nullptr : p.findTrack(trackId);
    if (primary && (primary->kind != primaryKind || primary->locked))
        primary = nullptr;
    if (trackId == kNewTrack)
        primary = p.findTrack(addTrack(p, primaryKind));
    if (primary && !isRangeFree(*primary, start, dur))
        start = nextFreeStart(*primary, start, dur);
    if (!primary) {
        primary = firstTrackWithSpace(p, primaryKind, start, dur);
        if (!primary)
            primary = p.findTrack(addTrack(p, primaryKind));
    }
    const QString primaryTrackId = primary->id;

    Clip c;
    c.id = newId();
    c.type = ClipType::Media;
    c.mediaId = mediaId;
    c.name = m->name;
    c.start = start;
    c.in = 0;
    c.out = dur;

    const bool linkAudio = withLinkedAudio && m->kind == MediaKind::Video && m->hasAudio;
    if (linkAudio) {
        Track *at = firstTrackWithSpace(p, TrackKind::Audio, start, dur);
        if (!at)
            at = p.findTrack(addTrack(p, TrackKind::Audio));
        c.linkId = newId();
        Clip a = c;
        a.id = newId();
        at->clips.append(a);
        at->sortClips();
    }
    Track *pt = p.findTrack(primaryTrackId); // addTrack may have reallocated
    pt->clips.append(c);
    pt->sortClips();
    return c.id;
}

static QString addGraphicClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, Clip c)
{
    start = snapToFrame(std::max<TimeUs>(0, start), p.canvas.fps);
    Track *t = trackId == kNewTrack ? p.findTrack(addTrack(p, TrackKind::Video)) : p.findTrack(trackId);
    // the requested track is busy at that time: use a new track right above it
    if (t && t->kind == TrackKind::Video && !t->locked && !isRangeFree(*t, start, dur))
        t = p.findTrack(addTrackNextTo(p, t->id, TrackKind::Video));
    if (!t || t->kind != TrackKind::Video || t->locked) {
        t = nullptr;
        for (int i = p.tracks.size() - 1; i >= 0; --i)
            if (p.tracks[i].kind == TrackKind::Video && !p.tracks[i].locked && isRangeFree(p.tracks[i], start, dur)) {
                t = &p.tracks[i];
                break;
            }
        if (!t)
            t = p.findTrack(addTrack(p, TrackKind::Video));
    }
    start = snapToFrame(std::max<TimeUs>(0, start), p.canvas.fps);
    if (!isRangeFree(*t, start, dur))
        start = nextFreeStart(*t, start, dur);
    c.id = newId();
    c.start = start;
    c.in = 0;
    c.out = dur;
    t->clips.append(c);
    t->sortClips();
    return c.id;
}

QString addTitleClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const TitleProps &props)
{
    Clip c;
    c.type = ClipType::Title;
    c.title = props;
    c.name = props.text.left(24);
    return addGraphicClip(p, trackId, start, dur, c);
}

QString addShapeClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const ShapeProps &props)
{
    Clip c;
    c.type = ClipType::Shape;
    c.shape = props;
    c.name = "Shape";
    return addGraphicClip(p, trackId, start, dur, c);
}

QString addEffectClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const ClipFx &fx, const QString &name)
{
    Clip c;
    c.type = ClipType::Effect;
    c.fx = fx;
    c.name = name.isEmpty() ? QString("Effect") : name;
    return addGraphicClip(p, trackId, start, dur, c);
}

QString insertClipCopy(Project &p, const QString &trackId, Clip c, TimeUs start)
{
    Track *t = p.findTrack(trackId);
    if (!t || t->locked)
        return {};
    c.id = newId();
    c.linkId.clear();
    c.start = nextFreeStart(*t, snapToFrame(start, p.canvas.fps), c.duration());
    t->clips.append(c);
    t->sortClips();
    return c.id;
}

bool splitClip(Project &p, const QString &clipId, TimeUs t, bool includeLinked)
{
    t = snapToFrame(t, p.canvas.fps);
    const QStringList ids = expandLinked(p, {clipId}, includeLinked);
    struct Target { QString trackId; QString clipId; };
    QVector<Target> targets;
    for (const QString &id : ids) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        if (!c || tr->locked)
            continue;
        if (t - c->start < kMinClipDuration || c->end() - t < kMinClipDuration)
            continue;
        targets.append({tr->id, id});
    }
    if (targets.isEmpty())
        return false;
    const QString newLink = targets.size() > 1 ? newId() : QString();
    for (const auto &tg : targets) {
        Track *tr = p.findTrack(tg.trackId);
        const int idx = tr->indexOfClip(tg.clipId);
        const Clip orig = tr->clips[idx];
        Clip left = orig;
        Clip right = orig;
        if (orig.type == ClipType::Media) {
            const TimeUs srcCut = orig.timelineToSource(t);
            left.out = srcCut;
            right.in = srcCut;
            if (orig.fx.speedCurve.isSet()) {
                // each half becomes a constant-speed clip of exactly the same length
                left.fx.speedCurve = SpeedCurve();
                right.fx.speedCurve = SpeedCurve();
                left.speed = std::max(0.05, double(left.out - left.in) / double(std::max<TimeUs>(1, t - orig.start)));
                right.speed = std::max(0.05, double(right.out - right.in) / double(std::max<TimeUs>(1, orig.end() - t)));
            }
        } else {
            left.in = 0;
            left.out = t - orig.start;
            right.in = 0;
            right.out = orig.end() - t;
        }
        left.fadeOut = 0;
        left.audio.fadeOut = 0;
        right.id = newId();
        right.start = t;
        right.fadeIn = 0;
        right.audio.fadeIn = 0;
        right.linkId = newLink;
        right.fx.transitionIn = TransitionSpec(); // a cut inside a clip is not a transition
        if (!orig.fx.keyframes.isEmpty()) {
            // keep the motion identical across the cut: both halves get a key at the cut, and an
            // eased segment that the cut divides is replaced by a dense linear approximation
            QVector<Keyframe> keys = orig.fx.keyframes;
            const TimeUs cutLocal = t - orig.start;
            for (int k = 0; k + 1 < keys.size(); ++k) {
                if (keys[k].t < cutLocal && cutLocal < keys[k + 1].t && keys[k].ease != "linear" && keys[k].ease != "hold") {
                    QVector<Keyframe> dense;
                    const int n = 12;
                    for (int j = 0; j <= n; ++j) {
                        const TimeUs tt = keys[k].t + (keys[k + 1].t - keys[k].t) * j / n;
                        const AnimatedValues v = Keyframes::evaluate(orig.fx.keyframes, tt);
                        dense << Keyframe{tt, v.x, v.y, v.scale, v.rotation, v.opacity, "linear"};
                    }
                    dense.last().ease = keys[k + 1].ease;
                    keys = keys.mid(0, k) + dense + keys.mid(k + 2);
                    break;
                }
            }
            Keyframe atCut;
            const AnimatedValues v = Keyframes::evaluate(keys, cutLocal);
            atCut.x = v.x;
            atCut.y = v.y;
            atCut.scale = v.scale;
            atCut.rotation = v.rotation;
            atCut.opacity = v.opacity;
            QVector<Keyframe> lk, rk;
            QString segEase = "linear"; // the curve of the segment the cut falls in
            for (const Keyframe &k : keys) {
                if (k.t < cutLocal) {
                    lk << k;
                    segEase = k.ease;
                } else if (k.t > cutLocal) {
                    Keyframe kk = k;
                    kk.t -= cutLocal;
                    rk << kk;
                } else {
                    segEase = k.ease;
                }
            }
            Keyframe l = atCut;
            l.t = cutLocal;
            lk << l;
            Keyframe r = atCut;
            r.t = 0;
            r.ease = segEase;
            rk.prepend(r);
            left.fx.keyframes = lk;
            right.fx.keyframes = rk;
        }
        // keep the original link group for the left halves
        tr->clips[idx] = left;
        tr->clips.append(right);
        tr->sortClips();
    }
    return true;
}

int splitAll(Project &p, TimeUs t)
{
    QStringList ids;
    for (const auto &tr : p.tracks)
        if (!tr.locked)
            for (const auto &c : tr.clips)
                if (c.containsTimeline(t) && c.start != t)
                    ids << c.id;
    int n = 0;
    QSet<QString> done;
    for (const QString &id : ids) {
        if (done.contains(id))
            continue;
        for (Clip *c : p.linkedClips(id))
            done.insert(c->id);
        if (splitClip(p, id, t, true))
            ++n;
    }
    return n;
}

TimeUs trimClip(Project &p, const QString &clipId, Edge edge, TimeUs newTime, bool includeLinked)
{
    Clip *primary = p.findClip(clipId);
    if (!primary)
        return -1;
    newTime = snapToFrame(newTime, p.canvas.fps);
    const TimeUs current = edge == Edge::Start ? primary->start : primary->end();
    TimeUs delta = newTime - current;
    const QStringList ids = expandLinked(p, {clipId}, includeLinked);
    // A speed curve is defined over the whole clip; trimming turns it into the equivalent
    // constant speed first so the remaining frames do not jump.
    for (const QString &id : ids)
        if (Clip *c = p.findClip(id); c && c->fx.speedCurve.isSet()) {
            const TimeUs d = std::max<TimeUs>(1, c->duration());
            c->fx.speedCurve = SpeedCurve();
            c->speed = std::max(0.05, double(c->out - c->in) / double(d));
        }

    // Compute the allowed delta range across all affected clips.
    TimeUs lo = std::numeric_limits<TimeUs>::min() / 4, hi = std::numeric_limits<TimeUs>::max() / 4;
    for (const QString &id : ids) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        if (!c || tr->locked)
            return current;
        const int idx = tr->indexOfClip(id);
        const double speed = c->speed;
        const TimeUs srcLen = mediaSourceLength(p, *c);
        if (edge == Edge::Start) {
            // start can move left until in reaches 0 / previous clip; right until min duration
            Q_UNUSED(srcLen);
            const TimeUs maxLeftBySource = c->type == ClipType::Media
                ? -TimeUs(std::floor(double(c->in) / speed))
                : std::numeric_limits<TimeUs>::min() / 4;
            TimeUs prevEnd = 0;
            if (idx > 0)
                prevEnd = tr->clips[idx - 1].end();
            lo = std::max({lo, maxLeftBySource, prevEnd - c->start, -c->start});
            hi = std::min(hi, c->duration() - kMinClipDuration);
        } else {
            TimeUs nextStart = std::numeric_limits<TimeUs>::max() / 4;
            if (idx + 1 < tr->clips.size())
                nextStart = tr->clips[idx + 1].start;
            const TimeUs maxRightBySource = srcLen >= 0 ? TimeUs(std::floor(double(srcLen - c->out) / speed))
                                                        : std::numeric_limits<TimeUs>::max() / 4;
            hi = std::min({hi, maxRightBySource, nextStart - c->end()});
            lo = std::max(lo, -(c->duration() - kMinClipDuration));
        }
    }
    if (lo > hi)
        return current;
    delta = std::clamp(delta, lo, hi);
    if (delta == 0)
        return current;
    for (const QString &id : ids) {
        Clip *c = p.findClip(id);
        const TimeUs srcDelta = TimeUs(std::llround(double(delta) * c->speed));
        if (edge == Edge::Start) {
            c->start += delta;
            c->in += srcDelta;
            for (Keyframe &k : c->fx.keyframes) // keyframes stay attached to the picture
                k.t -= delta;
            if (c->type != ClipType::Media) {
                c->in = 0;
                c->out -= delta;
            }
            c->fadeIn = std::min(c->fadeIn, c->duration());
            c->audio.fadeIn = std::min(c->audio.fadeIn, c->duration());
        } else {
            c->out += srcDelta;
            c->fadeOut = std::min(c->fadeOut, c->duration());
            c->audio.fadeOut = std::min(c->audio.fadeOut, c->duration());
        }
    }
    for (auto &t : p.tracks)
        t.sortClips();
    return current + delta;
}

bool moveClips(Project &p, const QStringList &clipIdsIn, TimeUs delta, const QString &primaryId,
               const QString &targetTrackId, bool includeLinked, TimeUs *appliedDelta)
{
    if (appliedDelta)
        *appliedDelta = 0;
    const QStringList ids = expandLinked(p, clipIdsIn, includeLinked);
    if (ids.isEmpty())
        return false;
    // Destination track per clip
    QHash<QString, QString> dest;
    for (const QString &id : ids) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        if (!c || tr->locked)
            return false;
        dest[id] = tr->id;
    }
    if (!primaryId.isEmpty() && !targetTrackId.isEmpty()) {
        Track *src = nullptr;
        p.findClip(primaryId, &src);
        Track *tgt = p.findTrack(targetTrackId);
        if (src && tgt && tgt->kind == src->kind && !tgt->locked)
            dest[primaryId] = tgt->id;
    }

    auto legal = [&](TimeUs d) -> bool {
        for (const QString &id : ids) {
            const Clip *c = p.findClip(id);
            const TimeUs ns = c->start + d;
            if (ns < 0)
                return false;
            const Track *tt = p.findTrack(dest[id]);
            for (const auto &o : tt->clips) {
                if (ids.contains(o.id))
                    continue;
                if (overlaps(ns, ns + c->duration(), o.start, o.end()))
                    return false;
            }
            // moving clips that end up on the same track must not overlap each other
            for (const QString &id2 : ids) {
                if (id2 == id || dest[id2] != dest[id])
                    continue;
                const Clip *c2 = p.findClip(id2);
                if (overlaps(ns, ns + c->duration(), c2->start + d, c2->end() + d))
                    return false;
            }
        }
        return true;
    };

    delta = snapToFrame(delta, p.canvas.fps);
    TimeUs applied = 0;
    if (legal(delta)) {
        applied = delta;
    } else {
        // Gather candidate deltas that butt the moving clips against obstacles.
        QVector<TimeUs> cands;
        for (const QString &id : ids) {
            const Clip *c = p.findClip(id);
            cands << -c->start;
            const Track *tt = p.findTrack(dest[id]);
            for (const auto &o : tt->clips) {
                if (ids.contains(o.id))
                    continue;
                cands << (o.end() - c->start) << (o.start - c->end());
            }
        }
        std::sort(cands.begin(), cands.end(), [delta](TimeUs a, TimeUs b) {
            return std::llabs(a - delta) < std::llabs(b - delta);
        });
        bool found = false;
        for (TimeUs cnd : cands) {
            // never jump to the other side of the original position
            if ((delta > 0 && cnd < 0) || (delta < 0 && cnd > 0))
                continue;
            if (legal(cnd)) {
                applied = cnd;
                found = true;
                break;
            }
        }
        const bool trackChange = !primaryId.isEmpty() && !targetTrackId.isEmpty();
        if (!found && !(trackChange && legal(0)))
            return false;
    }
    bool trackChanged = false;
    for (const QString &id : ids) {
        Track *tr = nullptr;
        p.findClip(id, &tr);
        if (tr->id != dest[id])
            trackChanged = true;
    }
    if (applied == 0 && !trackChanged)
        return false;

    // Apply: remove from source tracks, insert in destinations.
    QVector<Clip> moving;
    for (const QString &id : ids) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        Clip copy = *c;
        copy.start += applied;
        moving << copy;
        tr->clips.removeAt(tr->indexOfClip(id));
    }
    for (const Clip &c : moving) {
        Track *tt = p.findTrack(dest[c.id]);
        tt->clips.append(c);
        tt->sortClips();
    }
    if (appliedDelta)
        *appliedDelta = applied;
    return true;
}

bool deleteClips(Project &p, const QStringList &clipIdsIn, bool ripple, bool includeLinked)
{
    const QStringList ids = expandLinked(p, clipIdsIn, includeLinked);
    if (ids.isEmpty())
        return false;
    for (auto &t : p.tracks) {
        if (t.locked)
            continue;
        QVector<Clip> removed;
        for (int i = t.clips.size() - 1; i >= 0; --i)
            if (ids.contains(t.clips[i].id)) {
                removed << t.clips[i];
                t.clips.removeAt(i);
            }
        if (ripple) {
            // removed is sorted descending by start already
            for (const Clip &r : removed) {
                const TimeUs dur = r.duration();
                for (auto &c : t.clips)
                    if (c.start >= r.end())
                        c.start -= dur;
            }
        }
        t.sortClips();
    }
    // clean up orphaned link ids
    QHash<QString, int> counts;
    for (const auto &t : p.tracks)
        for (const auto &c : t.clips)
            if (!c.linkId.isEmpty())
                counts[c.linkId]++;
    for (auto &t : p.tracks)
        for (auto &c : t.clips)
            if (!c.linkId.isEmpty() && counts.value(c.linkId) < 2)
                c.linkId.clear();
    return true;
}

bool unlinkClips(Project &p, const QStringList &clipIds)
{
    bool any = false;
    for (const QString &id : clipIds)
        for (Clip *c : p.linkedClips(id)) {
            any = any || !c->linkId.isEmpty();
            c->linkId.clear();
        }
    return any;
}

bool linkClips(Project &p, const QStringList &clipIds)
{
    if (clipIds.size() < 2)
        return false;
    const QString link = newId();
    for (const QString &id : clipIds)
        if (Clip *c = p.findClip(id))
            c->linkId = link;
    return true;
}

bool setClipSpeed(Project &p, const QString &clipId, double speed, bool includeLinked)
{
    if (speed < 0.1 || speed > 16)
        return false;
    const QStringList ids = expandLinked(p, {clipId}, includeLinked);
    // Check that the new duration fits before the next clip on each track.
    for (const QString &id : ids) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        if (c->type != ClipType::Media)
            return false;
        const TimeUs newDur = TimeUs(std::llround(double(c->out - c->in) / speed));
        const int idx = tr->indexOfClip(id);
        if (idx + 1 < tr->clips.size() && c->start + newDur > tr->clips[idx + 1].start)
            return false;
    }
    for (const QString &id : ids) {
        Clip *c = p.findClip(id);
        c->speed = speed;
        c->fadeIn = std::min(c->fadeIn, c->duration());
        c->fadeOut = std::min(c->fadeOut, c->duration());
    }
    return true;
}

QVector<TimeUs> snapPoints(const Project &p, const QStringList &exclude)
{
    QVector<TimeUs> pts;
    pts << 0 << p.playhead;
    if (p.inPoint >= 0)
        pts << p.inPoint;
    if (p.outPoint >= 0)
        pts << p.outPoint;
    for (const Marker &m : p.markers) // beat markers make cutting to music easy
        pts << m.t;
    for (const auto &t : p.tracks)
        for (const auto &c : t.clips)
            if (!exclude.contains(c.id))
                pts << c.start << c.end();
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    return pts;
}

TimeUs snap(const QVector<TimeUs> &points, TimeUs t, TimeUs tolerance, bool *snapped)
{
    TimeUs best = t;
    TimeUs bestDist = tolerance + 1;
    for (TimeUs pnt : points) {
        const TimeUs d = std::llabs(pnt - t);
        if (d < bestDist) {
            bestDist = d;
            best = pnt;
        }
    }
    if (snapped)
        *snapped = bestDist <= tolerance;
    return bestDist <= tolerance ? best : t;
}

QVector<const Clip *> activeVideoClips(const Project &p, TimeUs t)
{
    QVector<const Clip *> out;
    for (const auto &tr : p.tracks) {
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        for (const auto &c : tr.clips) {
            if (c.start > t)
                break;
            if (c.enabled && c.type == ClipType::Media && c.containsTimeline(t)) {
                const MediaAsset *m = p.findMedia(c.mediaId);
                if (m && m->kind != MediaKind::Audio)
                    out << &c;
            }
        }
    }
    return out;
}

QVector<const Clip *> activeAudioClips(const Project &p, TimeUs t)
{
    QVector<const Clip *> out;
    for (const auto &tr : p.tracks) {
        if (tr.kind != TrackKind::Audio || tr.muted)
            continue;
        for (const auto &c : tr.clips) {
            if (c.start > t)
                break;
            if (c.enabled && !c.audio.mute && c.containsTimeline(t))
                out << &c;
        }
    }
    return out;
}

// ------------------------------------------------------------------ drag and drop placement

TimeUs insertPoint(const Track &t, TimeUs at, const QStringList &ignore)
{
    at = std::max<TimeUs>(0, at);
    for (const Clip &c : t.clips) {
        if (ignore.contains(c.id))
            continue;
        if (at > c.start && at < c.end())
            return (at - c.start) < (c.end() - at) ? c.start : c.end();
    }
    return at;
}

bool rippleInsertGap(Project &p, const QString &trackId, TimeUs at, TimeUs dur, const QStringList &ignore)
{
    if (dur <= 0)
        return true;
    const Track *start = p.findTrack(trackId);
    if (!start || start->locked)
        return false;
    // Collect the tracks to ripple: this one, plus the tracks of linked partners of every
    // clip that moves (repeated until nothing new is added).
    QSet<QString> tracks{trackId};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const Track &t : p.tracks) {
            if (!tracks.contains(t.id))
                continue;
            for (const Clip &c : t.clips) {
                if (c.start < at || c.linkId.isEmpty() || ignore.contains(c.id))
                    continue;
                for (Clip *l : p.linkedClips(c.id)) {
                    Track *lt = nullptr;
                    p.findClip(l->id, &lt);
                    if (lt && !tracks.contains(lt->id)) {
                        tracks.insert(lt->id);
                        grew = true;
                    }
                }
            }
        }
    }
    for (const QString &tid : tracks)
        if (const Track *t = p.findTrack(tid); !t || t->locked)
            return false;
    for (Track &t : p.tracks) {
        if (!tracks.contains(t.id))
            continue;
        for (Clip &c : t.clips)
            if (c.start >= at && !ignore.contains(c.id))
                c.start += dur;
        t.sortClips();
    }
    return true;
}

QStringList placeMedia(Project &p, const QStringList &mediaIds, const QString &trackIdIn, TimeUs at, PlaceMode mode)
{
    QStringList out;
    at = snapToFrame(std::max<TimeUs>(0, at), p.canvas.fps);
    auto kindOf = [&](const MediaAsset *m) { return m->kind == MediaKind::Audio ? TrackKind::Audio : TrackKind::Video; };
    auto durOf = [&](const MediaAsset *m) { return m->kind == MediaKind::Image ? 5 * kUsPerSec : m->duration; };
    QVector<const MediaAsset *> items;
    for (const QString &id : mediaIds)
        if (const MediaAsset *m = p.findMedia(id); m && durOf(m) > 0)
            items << m;
    if (items.isEmpty())
        return out;
    // One destination track per kind, decided up front so a multi-item drop stays together.
    QHash<int, QString> dest; // TrackKind -> track id
    QHash<int, TimeUs> cursor;
    const Track *dropTrack = (trackIdIn.isEmpty() || trackIdIn == kNewTrack) ? nullptr : p.findTrack(trackIdIn);
    if (dropTrack && dropTrack->locked)
        dropTrack = nullptr;
    for (TrackKind k : {TrackKind::Video, TrackKind::Audio}) {
        TimeUs total = 0;
        for (const MediaAsset *m : items)
            if (kindOf(m) == k)
                total += durOf(m);
        if (total == 0)
            continue;
        TimeUs t0 = at;
        QString tid;
        const bool onDropTrack = dropTrack && dropTrack->kind == k;
        if (mode == PlaceMode::NewTrack || trackIdIn == kNewTrack) {
            tid = onDropTrack ? addTrackNextTo(p, dropTrack->id, k) : addTrack(p, k);
        } else if (mode == PlaceMode::Insert) {
            tid = onDropTrack ? dropTrack->id : QString();
            if (tid.isEmpty())
                for (const Track &t : p.tracks)
                    if (t.kind == k && !t.locked) {
                        tid = t.id;
                        break;
                    }
            if (tid.isEmpty())
                tid = addTrack(p, k);
            t0 = insertPoint(*p.findTrack(tid), at);
            if (!rippleInsertGap(p, tid, t0, total))
                return {};
        } else if (onDropTrack) {
            tid = isRangeFree(*dropTrack, at, total) ? dropTrack->id : addTrackNextTo(p, dropTrack->id, k);
        } else {
            for (const Track &t : p.tracks)
                if (t.kind == k && !t.locked && isRangeFree(t, at, total)) {
                    tid = t.id;
                    break;
                }
            if (tid.isEmpty())
                tid = addTrack(p, k);
        }
        dest[int(k)] = tid;
        cursor[int(k)] = t0;
    }
    for (const MediaAsset *m : items) {
        const int k = int(kindOf(m));
        const QString cid = addMediaClip(p, m->id, dest[k], cursor[k]);
        if (cid.isEmpty())
            return {};
        cursor[k] = p.findClip(cid)->end();
        out << cid;
    }
    return out;
}

bool moveClipsPlaced(Project &p, const QStringList &clipIds, const QString &primaryId, TimeUs delta,
                     const QString &targetTrackId, PlaceMode mode, bool includeLinked)
{
    Track *src = nullptr;
    const Clip *primary = p.findClip(primaryId, &src);
    if (!primary || src->locked)
        return false;
    const TrackKind kind = src->kind;
    const QString srcId = src->id;
    delta = snapToFrame(delta, p.canvas.fps);

    if (mode == PlaceMode::NewTrack || targetTrackId == kNewTrack) {
        const QString nt = (!targetTrackId.isEmpty() && targetTrackId != kNewTrack) ? addTrackNextTo(p, targetTrackId, kind)
                                                                                     : addTrack(p, kind);
        return moveClips(p, clipIds, delta, primaryId, nt, includeLinked);
    }

    const QString tgt = targetTrackId.isEmpty() ? srcId : targetTrackId;
    const Track *tgtTrack = p.findTrack(tgt);
    if (!tgtTrack || tgtTrack->kind != kind || tgtTrack->locked)
        return false;

    if (mode == PlaceMode::Insert) {
        const QStringList ids = expandLinked(p, clipIds, includeLinked);
        // span of the moving clips that sit on the primary's track
        TimeUs gStart = primary->start, gEnd = primary->end();
        for (const QString &id : ids) {
            Track *t = nullptr;
            const Clip *c = p.findClip(id, &t);
            if (c && t->id == srcId) {
                gStart = std::min(gStart, c->start);
                gEnd = std::max(gEnd, c->end());
            }
        }
        const TimeUs want = std::max<TimeUs>(0, gStart + delta);
        const TimeUs t0 = insertPoint(*tgtTrack, want, ids);
        // Lift the moving clips out, open the gap, then drop them in.
        QVector<QPair<QString, Clip>> lifted; // destination track id, clip
        for (const QString &id : ids) {
            Track *t = nullptr;
            Clip *c = p.findClip(id, &t);
            if (!c)
                return false;
            lifted.append({(t->id == srcId) ? tgt : t->id, *c});
            t->clips.removeAt(t->indexOfClip(id));
        }
        if (!rippleInsertGap(p, tgt, t0, gEnd - gStart))
            return false;
        const TimeUs d = t0 - gStart;
        for (auto &[tid, c] : lifted) {
            c.start += d;
            Track *t = p.findTrack(tid);
            if (!isRangeFree(*t, c.start, c.duration())) {
                // a linked partner whose own track is busy there goes to a new track
                t = p.findTrack(addTrackNextTo(p, tid, t->kind));
            }
            t->clips.append(c);
            t->sortClips();
        }
        return true;
    }

    // Auto: exactly where it was dropped if that fits ...
    Project trial = p;
    TimeUs applied = 0;
    if (moveClips(trial, clipIds, delta, primaryId, tgt == srcId ? QString() : tgt, includeLinked, &applied) && applied == delta) {
        p = trial;
        return true;
    }
    // ... dropped on another track that is busy there: a new track next to it
    if (tgt != srcId) {
        Project trial2 = p;
        const QString nt = addTrackNextTo(trial2, tgt, kind);
        if (moveClips(trial2, clipIds, delta, primaryId, nt, includeLinked, &applied) && applied == delta) {
            p = trial2;
            return true;
        }
    }
    // ... otherwise the classic behaviour: as close as possible without overlapping
    return moveClips(p, clipIds, delta, primaryId, tgt == srcId ? QString() : tgt, includeLinked);
}

} // namespace TimelineOps
} // namespace ss

namespace ss {
namespace TimelineOps {

int removeSourceRanges(Project &p, const QString &clipId, QVector<QPair<TimeUs, TimeUs>> ranges)
{
    std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    int removed = 0;
    for (const auto &r : ranges) {
        Track *tr = nullptr;
        Clip *c = p.findClip(clipId, &tr);
        if (!c || tr->locked)
            break;
        const TimeUs a = std::max(r.first, c->in), b = std::min(r.second, c->out);
        if (b - a < 40 * kUsPerMs)
            continue;
        const TimeUs tA = snapToFrame(c->sourceToTimeline(a), p.canvas.fps);
        const TimeUs tB = snapToFrame(c->sourceToTimeline(b), p.canvas.fps);
        if (tB - tA < frameDurationUs(p.canvas.fps))
            continue;
        const QString trackId = tr->id;
        const TimeUs cEnd = c->end(), cStart = c->start;
        if (tB < cEnd - frameDurationUs(p.canvas.fps))
            splitClip(p, clipId, tB, true);
        QString middle = clipId;
        if (tA > cStart + frameDurationUs(p.canvas.fps)) {
            if (!splitClip(p, clipId, tA, true))
                continue;
            const Track *t2 = p.findTrack(trackId);
            middle.clear();
            for (const Clip &cl : t2->clips)
                if (cl.start == tA)
                    middle = cl.id;
        }
        if (middle.isEmpty())
            continue;
        if (middle == clipId) {
            // the range starts the clip: nothing earlier remains to process
            deleteClips(p, {middle}, true, true);
            ++removed;
            break;
        }
        if (deleteClips(p, {middle}, true, true))
            ++removed;
    }
    return removed;
}

QStringList defaultFillerWords()
{
    return {"um", "umm", "uh", "uhh", "uhm", "erm", "er", "ah", "ahh", "hmm", "mm", "mhm"};
}

QVector<QPair<TimeUs, TimeUs>> fillerWordRanges(const Transcript &t, const QStringList &fillers, TimeUs padding)
{
    QVector<QPair<TimeUs, TimeUs>> out;
    for (int i = 0; i < t.words.size(); ++i) {
        QString w = t.words[i].text.toLower();
        w.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
        if (w.isEmpty() || !fillers.contains(w))
            continue;
        TimeUs a = t.words[i].start - padding, b = t.words[i].end + padding;
        // never eat into the neighbouring words
        if (i > 0)
            a = std::max(a, t.words[i - 1].end);
        if (i + 1 < t.words.size())
            b = std::min(b, t.words[i + 1].start);
        if (!out.isEmpty() && a <= out.last().second)
            out.last().second = std::max(out.last().second, b);
        else if (b > a)
            out << qMakePair(a, b);
    }
    return out;
}

QString insertFreezeFrame(Project &p, const QString &clipId, TimeUs t, const QString &imageMediaId, TimeUs duration)
{
    Track *tr = nullptr;
    Clip *c = p.findClip(clipId, &tr);
    if (!c || tr->locked || !p.findMedia(imageMediaId))
        return {};
    t = snapToFrame(t, p.canvas.fps);
    duration = snapToFrame(duration, p.canvas.fps);
    const QString trackId = tr->id;
    // the still looks exactly like the frame it freezes (animated framing, colour, effects)
    const Clip frozen = c->animatedAt(std::clamp(t, c->start, c->end() - 1));
    if (t > c->start && t < c->end())
        splitClip(p, clipId, t, true);
    // tracks to open a gap on: the clip's own track and the tracks of its linked partners
    QSet<QString> tracks{trackId};
    for (Clip *l : p.linkedClips(clipId)) {
        Track *lt = nullptr;
        p.findClip(l->id, &lt);
        if (lt)
            tracks.insert(lt->id);
    }
    for (const QString &tid : tracks)
        if (const Track *t3 = p.findTrack(tid); !t3 || t3->locked)
            return {};
    // ripple every clip at/after t on those tracks, so nothing can collide
    for (auto &trk : p.tracks)
        if (tracks.contains(trk.id)) {
            for (auto &cl : trk.clips)
                if (cl.start >= t)
                    cl.start += duration;
            trk.sortClips();
        }
    Track *t2 = p.findTrack(trackId);
    if (!isRangeFree(*t2, t, duration))
        return {};
    Clip still;
    still.id = newId();
    still.type = ClipType::Media;
    still.mediaId = imageMediaId;
    still.name = "Freeze frame";
    still.start = t;
    still.in = 0;
    still.out = duration;
    still.transform = frozen.transform;
    still.color = frozen.color;
    still.fx = frozen.fx;
    still.fx.keyframes.clear();
    still.fx.transitionIn = TransitionSpec();
    still.fx.speedCurve = SpeedCurve();
    still.fx.audio = AudioFx();
    t2->clips.append(still);
    t2->sortClips();
    return still.id;
}

} // namespace TimelineOps
} // namespace ss
