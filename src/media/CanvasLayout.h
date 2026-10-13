#pragma once

#include "../captions/OverlayRenderer.h"
#include "../core/Model.h"

#include <QRectF>
#include <QVector>

namespace ss {

// Where everything visible at a given time sits on the canvas, and the edits behind
// dragging / resizing those things directly on the video (captions, titles, shapes and
// picture-in-picture video). Pure functions on Project so they are unit-testable and
// every change goes through Document::apply (undoable).
namespace CanvasLayout {

struct Item {
    enum Kind { Caption, Title, Shape, Media };
    Kind kind = Title;
    QString clipId;       // titles, shapes, media
    QString trackId;      // owning track (caption track for captions)
    int cueIndex = -1;    // transcript cue index (captions)
    QRectF rect;          // canvas pixels (project canvas size)
    bool locked = false;  // track is locked
    bool cueHasOwnPosition = false; // this caption was moved on its own
    QString label;        // short human name ("Caption", "Title", ...)
    bool isClip() const { return kind != Caption; }
    bool sameAs(const Item &o) const
    {
        return kind == o.kind && clipId == o.clipId && trackId == o.trackId && (kind != Caption || cueIndex == o.cueIndex);
    }
};

// Items under the playhead, top-most first (captions, then graphics, then video layers).
QVector<Item> itemsAt(const Project &p, const OverlayScene &scene, TimeUs t);

// Starting values captured when a drag begins.
struct Grab {
    Item item;
    bool perCue = false;  // captions: move only this caption instead of the whole track
    double x = 0, y = 0;  // title/shape centre (fraction), media posX/posY, caption offsetX/offsetY
    double w = 0, h = 0;  // shape size (fraction)
    double size = 0;      // title sizePct, caption sizePct, media scale
    double scale = 1;     // transform scale (keyframed titles/shapes)
    TimeUs localT = 0;    // playhead relative to the clip start (keyframe editing)
};
Grab grab(const Project &p, const Item &item, bool perCue);

// Moves the grabbed item by delta canvas pixels relative to where the drag started.
bool move(Project &p, const Grab &g, const QPointF &delta);
// Uniform resize relative to the grab (titles, captions, media; shapes keep their centre).
bool scale(Project &p, const Grab &g, double factor);
// Sets a shape's box directly (canvas pixels).
bool setShapeRect(Project &p, const Grab &g, const QRectF &rect);

bool resetPosition(Project &p, const Item &item);
bool resetSize(Project &p, const Item &item);

} // namespace CanvasLayout
} // namespace ss
