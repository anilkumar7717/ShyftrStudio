#pragma once

// Per-clip creative features: keyframes, transitions, effects, filters, chroma key, masks,
// blend modes, canvas background, speed curves and audio effects. All of it is evaluated
// locally (Qt for pictures, FFmpeg for sound); nothing needs a network connection.

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ss {

using TimeUs = qint64;

// Animated transform values. x/y mean posX/posY (fractions of the canvas) for video and
// the centre position (0..1) for titles and shapes.
struct Keyframe {
    TimeUs t = 0;            // relative to the clip start (timeline time)
    double x = 0, y = 0;
    double scale = 1.0;
    double rotation = 0;     // degrees
    double opacity = 1.0;
    QString ease = "smooth"; // linear | smooth | easeIn | easeOut | hold | bounce
};

struct AnimatedValues {
    double x = 0, y = 0, scale = 1, rotation = 0, opacity = 1;
};

namespace Keyframes {
// Interpolates the keyframes at clip-local time t (clamped to the first/last key).
AnimatedValues evaluate(const QVector<Keyframe> &keys, TimeUs t);
double ease(const QString &curve, double u);
int indexAt(const QVector<Keyframe> &keys, TimeUs t, TimeUs tolerance);
void sort(QVector<Keyframe> &keys);
QStringList easeNames();
} // namespace Keyframes

// Transition into this clip from the previous clip on the same track.
struct TransitionSpec {
    QString type;              // empty = cut. See Transitions::types()
    TimeUs duration = 500000;  // centred on the cut
    bool isSet() const { return !type.isEmpty() && duration > 0; }
};

struct EffectSpec {
    QString type;              // see VideoEffects::types()
    double intensity = 0.6;    // 0..1
    double speed = 1.0;        // animation rate for time-based effects
};

struct ChromaKey {
    bool enabled = false;
    QColor color = QColor(0, 255, 0);
    double similarity = 0.30;  // 0..1 distance treated as fully transparent
    double smoothness = 0.10;  // soft edge width
    double spill = 0.5;        // removes the key colour cast from edges
};

struct MaskSpec {
    QString type;              // "" | linear | mirror | circle | rect
    double cx = 0.5, cy = 0.5; // centre (fractions of the clip picture)
    double w = 0.6, h = 0.6;   // size (fractions); mirror uses h as band width
    double angle = 0;          // degrees
    double feather = 0.05;     // fraction of the picture size
    double radius = 0.1;       // rect corner radius (fraction of the smaller side)
    bool invert = false;
    bool isSet() const { return !type.isEmpty(); }
};

struct BackgroundFill {
    QString mode;              // "" | blur | color
    QColor color = Qt::black;
    double blur = 0.6;         // 0..1
    bool isSet() const { return !mode.isEmpty(); }
};

// Adjustments beyond the basic exposure/contrast/saturation/temperature/tint.
struct ExtraAdjust {
    double hue = 0;            // degrees, -180..180
    double vibrance = 0;       // -1..1
    double highlights = 0;     // -1..1
    double shadows = 0;        // -1..1
    double fade = 0;           // 0..1 (lifted, milky blacks)
    double vignette = 0;       // 0..1
    double grain = 0;          // 0..1
    double sharpen = 0;        // 0..1
    QString lut;               // optional .cube file
    double lutStrength = 1.0;
    bool isIdentity() const;
};

// Speed curve: speed multipliers over the clip (u = 0..1). The clip keeps its duration;
// the curve only redistributes time inside it (slow -> fast -> slow and so on).
struct SpeedCurve {
    QString preset;            // montage | hero | bullet | jumpCut | flashIn | flashOut | custom
    QVector<QPointF> points;   // (u, speed) sorted by u, speed > 0
    bool isSet() const { return points.size() >= 2; }
    // Fraction of the source range consumed at timeline fraction u (0..1), monotonic.
    double sourceFraction(double u) const;
    double timelineFraction(double sourceFraction) const; // inverse
    static SpeedCurve fromPreset(const QString &name);
    static QStringList presetNames();
};

struct AudioFx {
    QString voice;             // "" | chipmunk | deep | robot | echo | hall | telephone | megaphone | radio | underwater | monster
    bool denoise = false;
    bool enhance = false;      // voice clarity: high-pass, presence EQ, gentle compression
    bool isDefault() const { return voice.isEmpty() && !denoise && !enhance; }
    // FFmpeg audio filter chain (comma-joined, may be empty). Expects stereo float input.
    QString ffmpegChain(int sampleRate) const;
    static QStringList voiceNames();
};

struct ClipFx {
    QVector<Keyframe> keyframes;
    TransitionSpec transitionIn;
    QVector<EffectSpec> effects;
    ChromaKey chroma;
    MaskSpec mask;
    QString blend = "normal";
    BackgroundFill background;
    ExtraAdjust adjust;
    QString look;              // name of the applied filter preset (display only)
    SpeedCurve speedCurve;
    AudioFx audio;

    bool isDefault() const;
    // True when the picture needs the Qt compositor (anything FFmpeg's filter graph path
    // does not reproduce exactly).
    bool needsCompositor() const;
    QJsonObject toJson() const;
    static ClipFx fromJson(const QJsonObject &o);
};

struct Marker {
    TimeUs t = 0;
    QString label;
    QColor color = QColor("#FFD43B");
};

} // namespace ss
