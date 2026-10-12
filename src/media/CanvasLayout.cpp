#include "CanvasLayout.h"
#include "../core/TimelineOps.h"
#include "ExportGraph.h"

#include <algorithm>
#include <cmath>

namespace ss {
namespace CanvasLayout {

namespace {

const Track *trackOfClip(const Project &p, const QString &clipId)
{
    const Track *t = nullptr;
    p.findClip(clipId, &t);
    return t;
}

QRectF mediaRect(const Project &p, const Clip &c)
{
    const MediaAsset *m = p.findMedia(c.mediaId);
    if (!m)
        return {};
    const ClipGeometry g = computeGeometry(m->width, m->height, c.transform, p.canvas.width, p.canvas.height);
    return QRectF(g.x, g.y, g.outW, g.outH);
}

Cue *cueFor(Project &p, const QString &captionTrackId, int cueIndex)
{
    Track *t = p.findTrack(captionTrackId);
    if (!t)
        return nullptr;
    Transcript *tr = p.findTranscript(t->transcriptId);
    if (!tr || cueIndex < 0 || cueIndex >= tr->cues.size())
        return nullptr;
    return &tr->cues[cueIndex];
}

CaptionStyle trackStyle(const Project &p, const Track &t)
{
    return TemplateLibrary::instance().get(t.styleId).withOverrides(t.styleOverrides);
}

QString shortText(QString s)
{
    s.replace('\n', ' ');
    return s.size() > 24 ? s.left(22) + "…" : s;
}

} // namespace

QVector<Item> itemsAt(const Project &p, const OverlayScene &scene, TimeUs t)
{
    QVector<Item> out;
    const QSize frame(p.canvas.width, p.canvas.height);
    // captions are drawn last -> top-most
    for (int i = scene.captions.size() - 1; i >= 0; --i) {
        const auto &L = scene.captions[i];
        const CueInstance *ci = CaptionMapper::instanceAt(L.cues, t);
        if (!ci || ci->words.isEmpty())
            continue;
        Item it;
        it.kind = Item::Caption;
        it.trackId = L.trackId;
        it.cueIndex = ci->cueIndex;
        it.rect = CaptionRenderer::cueRect(frame, L.style.withOverrides(ci->overrides), *ci);
        const Track *tr = p.findTrack(L.trackId);
        it.locked = tr && tr->locked;
        it.cueHasOwnPosition = ci->overrides.contains("offsetXPct") || ci->overrides.contains("offsetYPct");
        it.label = "Caption";
        out << it;
    }
    for (int i = scene.graphics.size() - 1; i >= 0; --i) {
        const Clip &c = scene.graphics[i].clip;
        if (!c.containsTimeline(t))
            continue;
        Item it;
        it.clipId = c.id;
        const Track *tr = trackOfClip(p, c.id);
        it.trackId = tr ? tr->id : QString();
        it.locked = tr && tr->locked;
        if (c.type == ClipType::Title) {
            it.kind = Item::Title;
            it.rect = GraphicsRenderer::titleRect(frame, c);
            it.label = "Title “" + shortText(c.title.text) + "”";
        } else {
            it.kind = Item::Shape;
            it.rect = GraphicsRenderer::shapeRect(frame, c);
            it.label = "Shape";
        }
        out << it;
    }
    const auto media = TimelineOps::activeVideoClips(p, t);
    for (int i = media.size() - 1; i >= 0; --i) {
        const Clip *c = media[i];
        Item it;
        it.kind = Item::Media;
        it.clipId = c->id;
        const Track *tr = trackOfClip(p, c->id);
        it.trackId = tr ? tr->id : QString();
        it.locked = tr && tr->locked;
        it.rect = mediaRect(p, *c);
        it.label = c->name.isEmpty() ? QString("Video") : c->name;
        if (it.rect.isValid())
            out << it;
    }
    return out;
}

Grab grab(const Project &p, const Item &item, bool perCue)
{
    Grab g;
    g.item = item;
    if (item.kind == Item::Caption) {
        const Track *t = p.findTrack(item.trackId);
        if (!t)
            return g;
        CaptionStyle s = trackStyle(p, *t);
        g.perCue = perCue || item.cueHasOwnPosition;
        if (g.perCue) {
            const Transcript *tr = p.findTranscript(t->transcriptId);
            if (tr && item.cueIndex >= 0 && item.cueIndex < tr->cues.size())
                s = s.withOverrides(tr->cues[item.cueIndex].styleOverrides);
        }
        g.x = s.offsetXPct;
        g.y = s.offsetYPct;
        g.size = s.sizePct;
        return g;
    }
    const Clip *c = p.findClip(item.clipId);
    if (!c)
        return g;
    if (item.kind == Item::Title) {
        g.x = c->title.x;
        g.y = c->title.y;
        g.size = c->title.sizePct;
    } else if (item.kind == Item::Shape) {
        g.x = c->shape.x;
        g.y = c->shape.y;
        g.w = c->shape.w;
        g.h = c->shape.h;
    } else {
        g.x = c->transform.posX;
        g.y = c->transform.posY;
        g.size = c->transform.scale;
    }
    return g;
}

static void setCaptionValue(Project &p, const Grab &g, const char *key, double v)
{
    if (g.perCue) {
        if (Cue *cue = cueFor(p, g.item.trackId, g.item.cueIndex))
            cue->styleOverrides[key] = v;
    } else if (Track *t = p.findTrack(g.item.trackId)) {
        t->styleOverrides[key] = v;
    }
}

bool move(Project &p, const Grab &g, const QPointF &delta)
{
    const double W = std::max(1, p.canvas.width), H = std::max(1, p.canvas.height);
    const double fx = delta.x() / W, fy = delta.y() / H;
    switch (g.item.kind) {
    case Item::Caption:
        if (!p.findTrack(g.item.trackId))
            return false;
        setCaptionValue(p, g, "offsetXPct", std::clamp(g.x + fx, -0.5, 0.5));
        setCaptionValue(p, g, "offsetYPct", std::clamp(g.y - fy, -0.9, 0.9)); // + = up
        return true;
    case Item::Title: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->title.x = std::clamp(g.x + fx, 0.0, 1.0);
        c->title.y = std::clamp(g.y + fy, 0.0, 1.0);
        return true;
    }
    case Item::Shape: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->shape.x = std::clamp(g.x + fx, 0.0, 1.0);
        c->shape.y = std::clamp(g.y + fy, 0.0, 1.0);
        return true;
    }
    case Item::Media: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->transform.posX = std::clamp(g.x + fx, -1.5, 1.5);
        c->transform.posY = std::clamp(g.y + fy, -1.5, 1.5);
        return true;
    }
    }
    return false;
}

bool scale(Project &p, const Grab &g, double factor)
{
    if (!std::isfinite(factor) || factor <= 0)
        return false;
    switch (g.item.kind) {
    case Item::Caption:
        if (!p.findTrack(g.item.trackId))
            return false;
        setCaptionValue(p, g, "sizePct", std::clamp(g.size * factor, 0.015, 0.3));
        return true;
    case Item::Title: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->title.sizePct = std::clamp(g.size * factor, 0.01, 0.4);
        return true;
    }
    case Item::Shape: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->shape.w = std::clamp(g.w * factor, 0.005, 2.0);
        c->shape.h = std::clamp(g.h * factor, 0.002, 2.0);
        return true;
    }
    case Item::Media: {
        Clip *c = p.findClip(g.item.clipId);
        if (!c)
            return false;
        c->transform.scale = std::clamp(g.size * factor, 0.05, 10.0);
        return true;
    }
    }
    return false;
}

bool setShapeRect(Project &p, const Grab &g, const QRectF &rIn)
{
    Clip *c = p.findClip(g.item.clipId);
    if (!c || c->type != ClipType::Shape)
        return false;
    const double W = std::max(1, p.canvas.width), H = std::max(1, p.canvas.height);
    const QRectF r = rIn.normalized();
    c->shape.x = std::clamp(r.center().x() / W, 0.0, 1.0);
    c->shape.y = std::clamp(r.center().y() / H, 0.0, 1.0);
    c->shape.w = std::clamp(r.width() / W, 0.005, 2.0);
    c->shape.h = std::clamp(r.height() / H, 0.002, 2.0);
    return true;
}

bool resetPosition(Project &p, const Item &item)
{
    switch (item.kind) {
    case Item::Caption: {
        bool changed = false;
        if (item.cueHasOwnPosition) {
            if (Cue *cue = cueFor(p, item.trackId, item.cueIndex)) {
                changed = cue->styleOverrides.contains("offsetXPct") || cue->styleOverrides.contains("offsetYPct");
                cue->styleOverrides.remove("offsetXPct");
                cue->styleOverrides.remove("offsetYPct");
            }
        } else if (Track *t = p.findTrack(item.trackId)) {
            changed = t->styleOverrides.contains("offsetXPct") || t->styleOverrides.contains("offsetYPct");
            t->styleOverrides.remove("offsetXPct");
            t->styleOverrides.remove("offsetYPct");
        }
        return changed;
    }
    case Item::Title:
        if (Clip *c = p.findClip(item.clipId)) {
            c->title.x = 0.5;
            c->title.y = 0.5;
            return true;
        }
        return false;
    case Item::Shape:
        if (Clip *c = p.findClip(item.clipId)) {
            c->shape.x = 0.5;
            c->shape.y = 0.5;
            return true;
        }
        return false;
    case Item::Media:
        if (Clip *c = p.findClip(item.clipId)) {
            c->transform.posX = 0;
            c->transform.posY = 0;
            return true;
        }
        return false;
    }
    return false;
}

bool resetSize(Project &p, const Item &item)
{
    switch (item.kind) {
    case Item::Caption: {
        bool changed = false;
        if (Cue *cue = cueFor(p, item.trackId, item.cueIndex); cue && cue->styleOverrides.contains("sizePct")) {
            cue->styleOverrides.remove("sizePct");
            changed = true;
        }
        if (Track *t = p.findTrack(item.trackId); t && t->styleOverrides.contains("sizePct")) {
            t->styleOverrides.remove("sizePct");
            changed = true;
        }
        return changed;
    }
    case Item::Title:
        if (Clip *c = p.findClip(item.clipId)) {
            c->title.sizePct = TitleProps().sizePct;
            return true;
        }
        return false;
    case Item::Shape:
        return false;
    case Item::Media:
        if (Clip *c = p.findClip(item.clipId)) {
            c->transform.scale = 1.0;
            return true;
        }
        return false;
    }
    return false;
}

} // namespace CanvasLayout
} // namespace ss
