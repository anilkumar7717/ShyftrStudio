#pragma once

#include "Model.h"

namespace ss {

// Pure editing operations on a Project. All functions keep tracks sorted and
// guarantee that clips on a track never overlap. They return false (and leave the
// project untouched) when an operation is not legal.
namespace TimelineOps {

QString addTrack(Project &p, TrackKind kind, const QString &name = {});
// Inserts a new track at a position in p.tracks (clamped; tracks of a kind stay together).
QString addTrackAt(Project &p, TrackKind kind, int index, const QString &name = {});
// New track right next to `nextTo` on screen: above it for video, below it for audio.
QString addTrackNextTo(Project &p, const QString &nextTo, TrackKind kind);
// Pass as trackId to the add*Clip functions to put the clip on a brand-new track.
inline const QString kNewTrack = QStringLiteral("+new");
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
// Adjustment layer: its effects/filters apply to everything on the tracks below it.
QString addEffectClip(Project &p, const QString &trackId, TimeUs start, TimeUs dur, const ClipFx &fx, const QString &name = {});
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

// Cuts the given source-time ranges out of a clip (and its linked clips) and closes the
// gaps (ripple). Used by "remove silences" and "remove filler words". Returns ranges removed.
int removeSourceRanges(Project &p, const QString &clipId, QVector<QPair<TimeUs, TimeUs>> ranges);
// Source-time ranges of filler words ("um", "uh"...) in a transcript, padded slightly.
QVector<QPair<TimeUs, TimeUs>> fillerWordRanges(const Transcript &t, const QStringList &fillers, TimeUs padding = 30 * kUsPerMs);
QStringList defaultFillerWords();
// Inserts a still (image media) of `duration` at timeline time t on the clip's track,
// pushing the rest of that track (and linked audio) later. Returns the new clip id.
QString insertFreezeFrame(Project &p, const QString &clipId, TimeUs t, const QString &imageMediaId, TimeUs duration);

// ---- drag and drop placement
enum class PlaceMode {
    Auto,     // where it was dropped if free; otherwise on a new track next to that one
    Insert,   // push everything at/after the drop point later to make room (ripple insert)
    NewTrack, // always on a brand-new track
};
// The insert position for time t: t itself in a gap, else the nearer edge of the clip under t.
TimeUs insertPoint(const Track &t, TimeUs at, const QStringList &ignore = {});
// Opens a gap of `dur` at `at` on the track: clips starting at/after `at` (and their linked
// partners, with everything after them on the partners' tracks) move later. Ignored clips stay.
bool rippleInsertGap(Project &p, const QString &trackId, TimeUs at, TimeUs dur, const QStringList &ignore = {});
// Places media assets one after another starting at `at`. trackId may be empty (best track)
// or kNewTrack. Returns the primary clip ids (empty on failure).
QStringList placeMedia(Project &p, const QStringList &mediaIds, const QString &trackId, TimeUs at,
                       PlaceMode mode = PlaceMode::Auto);
// Drag-move of clips (the timeline's clip drag). delta/targetTrackId as for moveClips; with
// Auto a blocked drop onto another track goes to a new track; Insert pushes clips aside.
bool moveClipsPlaced(Project &p, const QStringList &clipIds, const QString &primaryId, TimeUs delta,
                     const QString &targetTrackId, PlaceMode mode, bool includeLinked = true);
// Moves a track to a new position among the tracks of its kind (index into p.tracks).
bool moveTrackTo(Project &p, const QString &trackId, int newIndex);

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
