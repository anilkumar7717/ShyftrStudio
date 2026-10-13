// Shyftr Studio - core project model.
// All timeline times are integer microseconds (TimeUs) on a single project time base,
// so mixed source frame rates never accumulate floating-point drift.
#pragma once

#include "Fx.h"

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ss {

using TimeUs = qint64;
constexpr TimeUs kUsPerSec = 1000000;
constexpr TimeUs kUsPerMs = 1000;

struct Rational {
    int num = 30;
    int den = 1;
    double toDouble() const { return den ? double(num) / double(den) : 0.0; }
    bool isValid() const { return num > 0 && den > 0; }
    QString toString() const { return QString("%1/%2").arg(num).arg(den); }
    static Rational fromString(const QString &s, Rational fallback = {30, 1});
    static Rational fromDouble(double fps);
    bool operator==(const Rational &o) const { return qint64(num) * o.den == qint64(o.num) * den; }
    bool operator!=(const Rational &o) const { return !(*this == o); }
};

// Frame helpers (exact integer arithmetic, rounding to nearest microsecond).
TimeUs framesToUs(qint64 frames, Rational fps);
qint64 usToFrameFloor(TimeUs t, Rational fps);
qint64 usToFrameRound(TimeUs t, Rational fps);
TimeUs snapToFrame(TimeUs t, Rational fps);
TimeUs frameDurationUs(Rational fps);
QString formatTimecode(TimeUs t, Rational fps);       // HH:MM:SS:FF
QString formatClock(TimeUs t, bool withMillis = true); // HH:MM:SS.mmm
QString newId();

enum class MediaKind { Video, Audio, Image };
enum class ClipType { Media, Title, Shape, Effect }; // Effect = adjustment layer applying effects to the tracks below
enum class TrackKind { Video, Audio, Caption };

QString toString(MediaKind k);
QString toString(ClipType k);
QString toString(TrackKind k);
MediaKind mediaKindFromString(const QString &s);
ClipType clipTypeFromString(const QString &s);
TrackKind trackKindFromString(const QString &s);

struct MediaAsset {
    QString id;
    QString name;
    QString path;          // absolute path at last save
    QString relativePath;  // relative to the project folder (used for portable projects)
    MediaKind kind = MediaKind::Video;
    TimeUs duration = 0;
    int width = 0;
    int height = 0;
    Rational fps{30, 1};
    bool variableFrameRate = false;
    bool hasVideo = false;
    bool hasAudio = false;
    int audioChannels = 0;
    int sampleRate = 0;
    int rotation = 0;     // display rotation already applied to width/height
    qint64 fileSize = 0;
    QString videoCodec;
    QString audioCodec;
    // runtime only
    bool missing = false;
};

struct Transform {
    double cropL = 0, cropT = 0, cropR = 0, cropB = 0; // fractions of source size
    double rotation = 0;                               // degrees, clockwise
    bool flipH = false;
    bool flipV = false;
    QString fit = "fit";                               // fit | fill | stretch
    double scale = 1.0;
    double posX = 0, posY = 0;                         // offset in fractions of canvas size
    double opacity = 1.0;
    bool isIdentity() const;
};

struct ColorAdjust {
    double exposure = 0;     // EV stops, -3..3
    double contrast = 1;     // 0..2
    double saturation = 1;   // 0..2
    double temperature = 0;  // -1 (cool) .. 1 (warm)
    double tint = 0;         // -1 (green) .. 1 (magenta)
    bool isIdentity() const;
};

struct AudioProps {
    double gainDb = 0;
    TimeUs fadeIn = 0;
    TimeUs fadeOut = 0;
    bool mute = false;
};

struct TitleProps {
    QString text = "Title";
    QString fontFamily = "Helvetica Neue";
    int weight = 700;
    bool italic = false;
    double sizePct = 0.08;            // of canvas height
    QColor color = Qt::white;
    QColor outlineColor = QColor(0, 0, 0, 0);
    double outlineWidthPct = 0.0;
    QColor backgroundColor = QColor(0, 0, 0, 0);
    double x = 0.5, y = 0.5;          // center position as fraction of canvas
    QString align = "center";         // left | center | right
    QString animation = "fade";       // entry: none | fade | slideUp | slideDown | slideLeft | slideRight | pop | zoom | typewriter | blur | bounce | spin | drop
    QString animationOut = "auto";    // auto (fade unless none) | none | fade | slideUp | slideDown | slideLeft | slideRight | zoom | pop | blur
    QString loop;                     // "" | pulse | shake | float | wiggle | blink | glow
    TimeUs animationDuration = 400 * kUsPerMs;
    QColor shadowColor = QColor(0, 0, 0, 0);
    double shadowOffsetPct = 0.06;    // of font size
    QColor gradientColor = QColor(0, 0, 0, 0); // bottom colour of a vertical gradient fill
    double letterSpacingPct = 0;
    double boxPaddingPct = 0.4;       // of font size
    double boxRadiusPct = 0.16;       // of font size
};

struct ShapeProps {
    QString shape = "rect";           // rect | ellipse | line | arrow | triangle | star
    QColor fill = QColor(255, 255, 255, 255);
    QColor stroke = QColor(0, 0, 0, 0);
    double strokeWidthPct = 0.0;
    double x = 0.5, y = 0.5, w = 0.3, h = 0.1; // center + size as fraction of canvas
    double radiusPct = 0.0;
};

struct Clip {
    QString id;
    ClipType type = ClipType::Media;
    QString mediaId;
    QString name;
    TimeUs start = 0;  // timeline position
    TimeUs in = 0;     // source in point
    TimeUs out = 0;    // source out point (exclusive)
    double speed = 1.0;
    QString linkId;    // clips sharing a linkId move/trim/split together
    bool enabled = true;
    TimeUs fadeIn = 0;   // video opacity fades
    TimeUs fadeOut = 0;
    Transform transform;
    ColorAdjust color;
    AudioProps audio;
    TitleProps title;
    ShapeProps shape;
    ClipFx fx;

    TimeUs duration() const;
    TimeUs end() const { return start + duration(); }
    TimeUs sourceToTimeline(TimeUs src) const;
    TimeUs timelineToSource(TimeUs t) const;
    bool containsTimeline(TimeUs t) const { return t >= start && t < end(); }
    // The clip with keyframed values applied at timeline time t.
    Clip animatedAt(TimeUs t) const;
    // Static (un-keyframed) values in keyframe terms, and the reverse.
    AnimatedValues baseValues() const;
    void setBaseValues(const AnimatedValues &v);
};

struct Track {
    QString id;
    TrackKind kind = TrackKind::Video;
    QString name;
    bool muted = false;
    bool locked = false;
    bool hidden = false;
    double volumeDb = 0;
    QVector<Clip> clips; // kept sorted by start

    // Caption tracks only
    QString transcriptId;
    QString styleId = "bold-highlight-yellow";
    QJsonObject styleOverrides;
    bool burnIn = true;
    // Audio tracks only: lower this track automatically while other audio (voice) plays.
    bool duck = false;

    void sortClips();
    int indexOfClip(const QString &clipId) const;
};

struct Word {
    QString text;
    TimeUs start = 0; // in the transcript's time domain (source time if bound to media)
    TimeUs end = 0;
    float confidence = -1.f; // 0..1, -1 = unknown
    bool edited = false;
    bool hardBreakBefore = false; // user-forced caption break before this word
};

struct Cue {
    int first = 0;  // first word index (inclusive)
    int last = 0;   // last word index (inclusive)
    int speaker = -1;
    QJsonObject styleOverrides; // per-caption overrides
    bool manual = false;        // created/edited by the user; preserved by re-segmentation
};

struct Speaker {
    int id = 0;
    QString name;
    QColor color;
};

struct Transcript {
    QString id;
    QString mediaId;    // empty => timeline time domain (e.g. imported SRT)
    QString language;
    QString model;
    QDateTime created;
    QVector<Word> words;
    QVector<Cue> cues;

    QString cueText(int cueIndex) const;
    TimeUs cueStart(int cueIndex) const;
    TimeUs cueEnd(int cueIndex) const;
    int cueAt(TimeUs t) const;
};

struct CanvasSettings {
    int width = 1920;
    int height = 1080;
    Rational fps{30, 1};
    int sampleRate = 48000;
    QColor background = Qt::black;
    QString aspectLabel() const;
};

struct Project {
    int schemaVersion = 0;
    QString id;
    QString name;
    QDateTime created;
    QDateTime modified;
    CanvasSettings canvas;
    QVector<MediaAsset> media;
    QVector<Track> tracks; // video tracks: later index = higher layer
    QVector<Transcript> transcripts;
    QVector<Speaker> speakers;
    TimeUs playhead = 0;
    TimeUs inPoint = -1;
    TimeUs outPoint = -1;
    QVector<Marker> markers;
    QString uiMode = "quick";
    QJsonObject exportSettings;

    MediaAsset *findMedia(const QString &id);
    const MediaAsset *findMedia(const QString &id) const;
    Track *findTrack(const QString &id);
    const Track *findTrack(const QString &id) const;
    Clip *findClip(const QString &id, Track **track = nullptr);
    const Clip *findClip(const QString &id, const Track **track = nullptr) const;
    Transcript *findTranscript(const QString &id);
    const Transcript *findTranscript(const QString &id) const;
    const Speaker *findSpeaker(int id) const;
    QVector<Clip *> linkedClips(const QString &clipId); // includes the clip itself
    TimeUs duration() const;
    int trackIndex(const QString &id) const;
    QVector<int> trackIndicesOfKind(TrackKind k) const;
};

Project makeEmptyProject(const QString &name, const CanvasSettings &canvas);

} // namespace ss
