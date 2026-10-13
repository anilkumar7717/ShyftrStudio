#pragma once

#include "../captions/OverlayRenderer.h"
#include "../core/Model.h"

#include <QImage>
#include <QSet>

namespace ss {

// Supplies decoded pictures. The preview implements it with Qt Multimedia players; the
// export implements it with FFmpeg decoders (FrameReader). Pictures must be in display
// orientation and may be any resolution (the compositor scales them).
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual QImage frame(const Clip &clip, const MediaAsset &media, TimeUs sourceTime) = 0;
};

// One renderer for everything visible: video layers with keyframes, transitions, effects,
// filters, chroma key, masks, blend modes and canvas backgrounds; titles, shapes and effect
// layers in track order; burned-in captions on top. Preview and export both call it, so
// what you see is what gets exported.
namespace Compositor {

struct Need {
    QString clipId;
    QString mediaId;
    TimeUs sourceTime = 0;
};
// Media pictures needed to draw time t (includes the outgoing/incoming clips of transitions).
QVector<Need> needsAt(const Project &p, TimeUs t);

struct Options {
    bool captions = true;           // draw caption tracks from the scene
    bool missingPlaceholder = true; // draw an "offline" card for missing media
};

// Renders the frame at t into canvas (any size with the project's aspect ratio).
void render(QImage &canvas, const Project &p, const OverlayScene &scene, TimeUs t, FrameSource &source,
            const Options &opt = Options());

// True if the project uses anything FFmpeg's filter-graph export path does not reproduce
// (keyframes, transitions, effects, filters, masks, effect layers, speed curves...).
bool projectNeedsCompositor(const Project &p);

// Local playback rate of a clip at t (handles speed curves).
double sourceRateAt(const Clip &c, TimeUs t);

} // namespace Compositor
} // namespace ss
