#pragma once

// CPU image processing shared by the preview and the export compositor, so a frame looks
// the same in both. Sizes are expressed as fractions of the picture, which keeps effects
// identical whether the preview renders at 960 px or the export at 3840 px.

#include "../core/Fx.h"
#include "../core/Model.h"

#include <QImage>
#include <QPainter>

namespace ss {
namespace ImageFx {

struct Info {
    QString id;
    QString label;
    QString group;
};

// ---- blur helpers (premultiplied ARGB32)
void boxBlur(QImage &img, int radius, int passes = 3);
QImage blurred(const QImage &img, double radiusPx); // fast for large radii (downscale, blur, upscale)

// ---- clip picture operations
void chromaKey(QImage &img, const ChromaKey &ck);
void applyMask(QImage &img, const MaskSpec &m);
void colorAdjust(QImage &img, const ExtraAdjust &a, quint32 seed);
bool applyLut(QImage &img, const QString &cubePath, double strength, QString *error = nullptr);

// ---- video effects (on a clip picture, or the whole canvas for effect layers)
QVector<Info> effects();
QString effectLabel(const QString &id);
// timeSec: project time; clipProgress: 0..1 inside the clip; seed: per-frame random seed
void applyEffect(QImage &img, const EffectSpec &e, double timeSec, double clipProgress, quint32 seed);
// Effects that move the picture instead of changing pixels (shake, zoom pulse, Ken Burns).
// dx/dy are fractions of the canvas; scale multiplies; rotation adds degrees.
void effectMotion(const EffectSpec &e, double timeSec, double clipProgress, double *dx, double *dy, double *scale,
                  double *rotation);
bool isMotionEffect(const QString &id);

// ---- blend modes
QStringList blendModes();
QPainter::CompositionMode compositionMode(const QString &blend);

// ---- transitions: draws the blend of the outgoing layer A and incoming layer B (both
// canvas-sized, transparent where empty) at progress 0..1.
QVector<Info> transitions();
QString transitionLabel(const QString &id);
void drawTransition(QPainter &p, const QImage &a, const QImage &b, const QString &type, double progress, quint32 seed);

// ---- filter presets ("looks")
struct Look {
    QString id;
    QString label;
    ColorAdjust color;
    ExtraAdjust adjust;
};
QVector<Look> looks();

quint32 hash32(quint32 x);

} // namespace ImageFx
} // namespace ss
