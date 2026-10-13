#include "Compositor.h"
#include "ColorMath.h"
#include "ExportGraph.h"
#include "ImageFx.h"

#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace ss {
namespace Compositor {

namespace {

struct TrackPlan {
    const Clip *single = nullptr;
    const Clip *a = nullptr; // outgoing
    const Clip *b = nullptr; // incoming
    double progress = 0;
};

TrackPlan planTrack(const Track &tr, TimeUs t, Rational fps)
{
    TrackPlan plan;
    const TimeUs frame = frameDurationUs(fps);
    for (int i = 1; i < tr.clips.size(); ++i) {
        const Clip &b = tr.clips[i];
        const Clip &a = tr.clips[i - 1];
        if (!b.enabled || !a.enabled || !b.fx.transitionIn.isSet() || b.type == ClipType::Effect || a.type == ClipType::Effect)
            continue;
        if (std::llabs(a.end() - b.start) > frame)
            continue;
        const TimeUs d = std::min({b.fx.transitionIn.duration, a.duration(), b.duration()});
        if (d <= 0)
            continue;
        const TimeUs t0 = b.start - d / 2;
        if (t >= t0 && t < t0 + d) {
            plan.a = &a;
            plan.b = &b;
            plan.progress = double(t - t0) / double(d);
            return plan;
        }
    }
    for (const Clip &c : tr.clips) {
        if (c.start > t)
            break;
        if (c.enabled && c.containsTimeline(t)) {
            plan.single = &c;
            break;
        }
    }
    return plan;
}

TimeUs sourceFor(const Clip &c, const MediaAsset &m, TimeUs t, Rational fps)
{
    TimeUs s = c.timelineToSource(t);
    if (m.kind == MediaKind::Video && m.duration > 0)
        s = std::clamp<TimeUs>(s, 0, std::max<TimeUs>(0, m.duration - frameDurationUs(fps)));
    return std::max<TimeUs>(0, s);
}

quint32 seedFor(const Clip &c, TimeUs t)
{
    return ImageFx::hash32(quint32(qint64(t / 1000)) * 2654435761u ^ quint32(qHash(c.id)));
}

double progressIn(const Clip &c, TimeUs t)
{
    return std::clamp(double(t - c.start) / double(std::max<TimeUs>(1, c.duration())), 0.0, 1.0);
}

void applyMotion(Clip &c, TimeUs t)
{
    double dx = 0, dy = 0, sc = 1, rot = 0;
    bool any = false;
    for (const EffectSpec &e : c.fx.effects)
        if (ImageFx::isMotionEffect(e.type)) {
            ImageFx::effectMotion(e, double(t) / kUsPerSec, progressIn(c, t), &dx, &dy, &sc, &rot);
            any = true;
        }
    if (!any)
        return;
    if (c.type == ClipType::Title) {
        c.title.x += dx;
        c.title.y += dy;
    } else if (c.type == ClipType::Shape) {
        c.shape.x += dx;
        c.shape.y += dy;
    } else {
        c.transform.posX += dx;
        c.transform.posY += dy;
    }
    c.transform.scale *= sc;
    c.transform.rotation += rot;
}

// Pixel processing shared by clip pictures and layers.
void processPixels(QImage &img, const Clip &c, TimeUs t)
{
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (c.fx.chroma.enabled)
        ImageFx::chromaKey(img, c.fx.chroma);
    if (!c.color.isIdentity()) {
        QImage straight = img.convertToFormat(QImage::Format_ARGB32);
        ColorMath::apply(straight, c.color);
        img = straight.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    const quint32 seed = seedFor(c, t);
    if (!c.fx.adjust.isIdentity()) {
        ImageFx::colorAdjust(img, c.fx.adjust, seed);
        if (!c.fx.adjust.lut.isEmpty())
            ImageFx::applyLut(img, c.fx.adjust.lut, c.fx.adjust.lutStrength);
    }
    for (const EffectSpec &e : c.fx.effects)
        if (!ImageFx::isMotionEffect(e.type))
            ImageFx::applyEffect(img, e, double(t) / kUsPerSec, progressIn(c, t), seed);
    if (c.fx.mask.isSet())
        ImageFx::applyMask(img, c.fx.mask);
}

bool needsPixelWork(const Clip &c)
{
    if (c.fx.chroma.enabled || c.fx.mask.isSet() || !c.fx.adjust.isIdentity() || !c.color.isIdentity())
        return true;
    for (const EffectSpec &e : c.fx.effects)
        if (!ImageFx::isMotionEffect(e.type))
            return true;
    return false;
}

QImage rotateToDisplay(const QImage &src, const MediaAsset &m)
{
    // Decoders may hand over frames in coded orientation; rotate to display orientation
    // only when the picture clearly has the swapped aspect (never for near-square media).
    if (src.isNull() || m.width <= 0 || m.height <= 0)
        return src;
    const double ra = double(src.width()) / std::max(1, src.height());
    const double ma = double(m.width) / m.height;
    if (std::abs(std::log(ra) + std::log(ma)) < 0.5 * std::abs(std::log(ra) - std::log(ma))) {
        QTransform rot;
        rot.rotate(m.rotation == 270 ? -90 : 90);
        return src.transformed(rot);
    }
    return src;
}

void drawBackground(QPainter &p, const QSize &out, const Clip &c, const QImage &full)
{
    const BackgroundFill &bg = c.fx.background;
    if (bg.mode == "color") {
        p.fillRect(QRect(QPoint(0, 0), out), bg.color);
    } else if (bg.mode == "blur" && !full.isNull()) {
        const QImage cover = full.scaled(out, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QImage crop = cover.copy((cover.width() - out.width()) / 2, (cover.height() - out.height()) / 2, out.width(),
                                       out.height());
        p.drawImage(0, 0, ImageFx::blurred(crop, (0.01 + std::clamp(bg.blur, 0.0, 1.0) * 0.05) * out.width()));
    }
}

void drawMedia(QPainter &p, const QSize &out, const Project &proj, const Clip &cIn, const MediaAsset &m,
               const QImage &frameIn, TimeUs evalT)
{
    Q_UNUSED(proj);
    Clip c = cIn.animatedAt(evalT);
    applyMotion(c, evalT);
    const QImage src = rotateToDisplay(frameIn, m);
    if (src.isNull())
        return;
    if (c.fx.background.isSet())
        drawBackground(p, out, c, src);
    // geometry directly in output pixels: correct for any output size or aspect
    const ClipGeometry g = computeGeometry(m.width, m.height, c.transform, out.width(), out.height());
    const double k = 1.0;
    const double sx = double(src.width()) / std::max(1, m.width);
    const double sy = double(src.height()) / std::max(1, m.height);
    const QRect crop(int(g.crop.x() * sx), int(g.crop.y() * sy), std::max(1, int(g.crop.width() * sx)),
                     std::max(1, int(g.crop.height() * sy)));
    QImage part = src.copy(crop);
    double drawW = g.crop.width() * g.scaleX * k;
    double drawH = g.crop.height() * g.scaleY * k;
    if (g.quarterTurns % 2 == 1) {
        drawW = g.crop.width() * g.scaleY * k;
        drawH = g.crop.height() * g.scaleX * k;
    }
    if (needsPixelWork(c)) {
        // work at (about) the size it will be drawn: identical look at every resolution, and fast
        const double pw = g.quarterTurns % 2 == 1 ? drawH : drawW;
        const double ph = g.quarterTurns % 2 == 1 ? drawW : drawH;
        if (part.width() > pw * 1.25 && part.height() > ph * 1.25)
            part = part.scaled(std::max(2, int(std::ceil(pw))), std::max(2, int(std::ceil(ph))), Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
        processPixels(part, c, evalT);
    }
    const double opacity = GraphicsRenderer::clipOpacity(c, evalT);
    if (opacity <= 0.001)
        return;
    const QPointF center((g.x + g.outW / 2) * k, (g.y + g.outH / 2) * k);
    p.save();
    p.setOpacity(opacity);
    p.setCompositionMode(ImageFx::compositionMode(c.fx.blend));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(center);
    p.rotate(g.quarterTurns * 90 + g.freeAngle);
    p.scale(c.transform.flipH ? -1 : 1, c.transform.flipV ? -1 : 1);
    p.drawImage(QRectF(-drawW / 2, -drawH / 2, drawW, drawH), part);
    p.restore();
}

void drawGraphic(QPainter &p, const QSize &out, const Clip &cIn, TimeUs evalT)
{
    Clip c = cIn;
    // Motion effects move the graphic; keyframes are applied inside the renderer.
    if (!c.fx.effects.isEmpty()) {
        Clip anim = c.animatedAt(evalT);
        Clip moved = anim;
        applyMotion(moved, evalT);
        if (moved.fx.keyframes.isEmpty()) {
            c = moved;
        } else {
            // shift the keys by the motion delta so the renderer's own evaluation lands on it
            const AnimatedValues base = anim.baseValues(), now = moved.baseValues();
            for (Keyframe &kf : c.fx.keyframes) {
                kf.x += now.x - base.x;
                kf.y += now.y - base.y;
                kf.scale *= base.scale != 0 ? now.scale / base.scale : 1.0;
                kf.rotation += now.rotation - base.rotation;
            }
        }
    }
    const bool layer = needsPixelWork(c) || c.fx.blend != "normal";
    if (!layer) {
        if (c.type == ClipType::Title)
            GraphicsRenderer::paintTitle(p, out, c, evalT);
        else
            GraphicsRenderer::paintShape(p, out, c, evalT);
        return;
    }
    QImage img(out, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter q(&img);
        if (c.type == ClipType::Title)
            GraphicsRenderer::paintTitle(q, out, c, evalT);
        else
            GraphicsRenderer::paintShape(q, out, c, evalT);
    }
    processPixels(img, c, evalT);
    p.save();
    p.setCompositionMode(ImageFx::compositionMode(c.fx.blend));
    p.drawImage(0, 0, img);
    p.restore();
}

// Draws one clip of a track into the painter (canvas or transition layer).
void drawClip(QPainter &p, const QSize &out, const Project &proj, const Clip &c, TimeUs t, TimeUs evalT, FrameSource &src,
              const Options &opt)
{
    if (c.type == ClipType::Title || c.type == ClipType::Shape) {
        drawGraphic(p, out, c, evalT);
        return;
    }
    if (c.type != ClipType::Media)
        return;
    const MediaAsset *m = proj.findMedia(c.mediaId);
    if (!m || m->kind == MediaKind::Audio)
        return;
    if (m->missing) {
        if (opt.missingPlaceholder) {
            p.fillRect(QRect(QPoint(0, 0), out), QColor(60, 20, 20));
            p.setPen(Qt::white);
            p.drawText(QRect(QPoint(0, 0), out), Qt::AlignCenter, "Media offline: " + m->name + "\nUse File → Relink Media");
        }
        return;
    }
    const QImage frame = src.frame(c, *m, sourceFor(c, *m, t, proj.canvas.fps));
    if (!frame.isNull())
        drawMedia(p, out, proj, c, *m, frame, evalT);
}

void applyEffectLayer(QImage &canvas, const Project &proj, const Clip &c, TimeUs t)
{
    Q_UNUSED(proj);
    const double opacity = GraphicsRenderer::clipOpacity(c, t);
    if (opacity <= 0.001)
        return;
    QImage fx = canvas.copy();
    // motion effects move the whole picture below
    double dx = 0, dy = 0, sc = 1, rot = 0;
    bool motion = false;
    for (const EffectSpec &e : c.fx.effects)
        if (ImageFx::isMotionEffect(e.type)) {
            ImageFx::effectMotion(e, double(t) / kUsPerSec, progressIn(c, t), &dx, &dy, &sc, &rot);
            motion = true;
        }
    if (motion) {
        QImage moved(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        moved.fill(Qt::black);
        QPainter q(&moved);
        q.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF ctr(canvas.width() / 2.0, canvas.height() / 2.0);
        q.translate(ctr + QPointF(dx * canvas.width(), dy * canvas.height()));
        q.rotate(rot);
        q.scale(sc, sc);
        q.translate(-ctr);
        q.drawImage(0, 0, fx);
        q.end();
        fx = moved;
    }
    Clip noMask = c;
    processPixels(fx, noMask, t);
    QPainter p(&canvas);
    p.setOpacity(opacity);
    p.setCompositionMode(ImageFx::compositionMode(c.fx.blend));
    p.drawImage(0, 0, fx);
}

} // namespace

QVector<Need> needsAt(const Project &p, TimeUs t)
{
    QVector<Need> out;
    for (const Track &tr : p.tracks) {
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        const TrackPlan plan = planTrack(tr, t, p.canvas.fps);
        for (const Clip *c : {plan.single, plan.a, plan.b}) {
            if (!c || c->type != ClipType::Media)
                continue;
            const MediaAsset *m = p.findMedia(c->mediaId);
            if (!m || m->kind == MediaKind::Audio)
                continue;
            out << Need{c->id, c->mediaId, sourceFor(*c, *m, t, p.canvas.fps)};
        }
    }
    return out;
}

void render(QImage &canvas, const Project &proj, const OverlayScene &scene, TimeUs t, FrameSource &src, const Options &opt)
{
    if (canvas.format() != QImage::Format_ARGB32_Premultiplied)
        canvas = canvas.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    canvas.fill(proj.canvas.background);
    const QSize out = canvas.size();
    for (const Track &tr : proj.tracks) {
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        const TrackPlan plan = planTrack(tr, t, proj.canvas.fps);
        if (plan.a && plan.b) {
            QImage la(out, QImage::Format_ARGB32_Premultiplied), lb(out, QImage::Format_ARGB32_Premultiplied);
            la.fill(Qt::transparent);
            lb.fill(Qt::transparent);
            {
                QPainter qa(&la);
                qa.setRenderHint(QPainter::Antialiasing);
                drawClip(qa, out, proj, *plan.a, t, std::min(t, plan.a->end() - 1), src, opt);
            }
            {
                QPainter qb(&lb);
                qb.setRenderHint(QPainter::Antialiasing);
                drawClip(qb, out, proj, *plan.b, t, std::max(t, plan.b->start), src, opt);
            }
            QPainter p(&canvas);
            p.setRenderHint(QPainter::Antialiasing);
            ImageFx::drawTransition(p, la, lb, plan.b->fx.transitionIn.type, plan.progress, seedFor(*plan.b, t));
        } else if (plan.single) {
            if (plan.single->type == ClipType::Effect) {
                applyEffectLayer(canvas, proj, *plan.single, t);
            } else {
                QPainter p(&canvas);
                p.setRenderHint(QPainter::Antialiasing);
                drawClip(p, out, proj, *plan.single, t, t, src, opt);
            }
        }
    }
    if (opt.captions) {
        QPainter p(&canvas);
        scene.renderCaptions(p, out, t);
    }
}

bool projectNeedsCompositor(const Project &p)
{
    int topMediaTrack = -1;
    for (int i = 0; i < p.tracks.size(); ++i) {
        const Track &tr = p.tracks[i];
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        for (const Clip &c : tr.clips) {
            if (!c.enabled)
                continue;
            if (c.type == ClipType::Effect || c.fx.needsCompositor())
                return true;
            if (c.type == ClipType::Media)
                topMediaTrack = i;
        }
    }
    // the filter-graph path draws titles/shapes above all video; if one sits below a video
    // track, only the compositor keeps the track order
    for (int i = 0; i < topMediaTrack && i < p.tracks.size(); ++i) {
        const Track &tr = p.tracks[i];
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        for (const Clip &c : tr.clips)
            if (c.enabled && (c.type == ClipType::Title || c.type == ClipType::Shape))
                return true;
    }
    return false;
}

double sourceRateAt(const Clip &c, TimeUs t)
{
    if (!c.fx.speedCurve.isSet())
        return c.speed;
    const TimeUs d = std::max<TimeUs>(1, c.duration());
    const double u = std::clamp(double(t - c.start) / d, 0.0, 1.0);
    const double du = 0.01;
    const double a = c.fx.speedCurve.sourceFraction(std::max(0.0, u - du / 2));
    const double b = c.fx.speedCurve.sourceFraction(std::min(1.0, u + du / 2));
    const double span = std::min(1.0, u + du / 2) - std::max(0.0, u - du / 2);
    return span > 0 ? (b - a) / span * double(c.out - c.in) / double(d) : c.speed;
}

} // namespace Compositor
} // namespace ss
