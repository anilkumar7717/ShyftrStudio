#pragma once

#include "Model.h"

namespace ss {

// Pure editing operations on a Project. All functions keep tracks sorted and
// guarantee that clips on a track never overlap. They return false (and leave the
// project untouched) when an operation is not legal.
namespace TimelineOps {

QString addTrack(Project &p, TrackKind kind, const QString &name = {});
bool removeTrack(Project &p, const QString &trackId);
bool moveTrack(Project &p, const QString &trackId, int delta);

// Is [start, start+dur) free on the track (ignoring the given clip ids)?
bool isRangeFree(const Track &t, TimeUs start, TimeUs dur, const QStringList &ignore = {});
// First free position >= start on the track that can hold dur.
TimeUs nextFreeStart(const Track &t, TimeUs start, TimeUs dur);

// Places a media asset on the timeline. For video with audio a linked audio clip is
// created on an audio track. Returns the primary clip id or empty on failure.
QString addMediaClip(Project &p, const QString &mediaId, const QString &trackId, TimeUs start,
                     bool withLinkedAudio = true, TimeUs imageDuration = 5 * kUsPerSec);
QString addTitleClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const TitleProps &props);
QString addShapeClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const ShapeProps &props);
// Inserts a clip copy (used by paste / duplicate). Picks next free position.
QString insertClipCopy(Project &p, const QString &trackId, Clip c, TimeUs start);

// Splits the clip (and its linked clips) at timeline time t.
bool splitClip(Project &p, const QString &clipId, TimeUs t, bool includeLinked = true);
// Splits every clip under t on unlocked tracks.
int splitAll(Project &p, TimeUs t);

enum class Edge { Start, End };
// Trims an edge to the requested timeline time, clamped to source bounds and neighbours.
// Returns the applied time.
TimeUs trimClip(Project &p, const QString &clipId, Edge edge, TimeUs newTime, bool includeLinked = true);

// Moves clips by delta. If targetTrackId is set the primary clip changes track (same kind).
// Clamps the delta so nothing overlaps. Returns false if nothing moved.
bool moveClips(Project &p, const QStringList &clipIds, TimeUs delta, const QString &primaryId = {},
               const QString &targetTrackId = {}, bool includeLinked = true, TimeUs *appliedDelta = nullptr);

// Deletes clips (and linked partners). Ripple closes the gap on each affected track.
bool deleteClips(Project &p, const QStringList &clipIds, bool ripple, bool includeLinked = true);

bool unlinkClips(Project &p, const QStringList &clipIds);
bool linkClips(Project &p, const QStringList &clipIds);

bool setClipSpeed(Project &p, const QString &clipId, double speed, bool includeLinked = true);

// Snap candidates (clip edges, playhead, in/out marks), excluding the given clips.
QVector<TimeUs> snapPoints(const Project &p, const QStringList &exclude = {});
TimeUs snap(const QVector<TimeUs> &points, TimeUs t, TimeUs tolerance, bool *snapped = nullptr);

// Finds the top-most enabled visible clip of the given kind at time t.
QVector<const Clip *> activeVideoClips(const Project &p, TimeUs t); // bottom -> top
QVector<const Clip *> activeAudioClips(const Project &p, TimeUs t);

// Media helpers
TimeUs mediaSourceLength(const Project &p, const Clip &c); // -1 = unbounded (images/titles)

} // namespace TimelineOps
} // namespace ss
