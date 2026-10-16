#pragma once

#include "../core/Model.h"
#include "CaptionEngine.h"
#include "CaptionStyle.h"

#include <QImage>
#include <QPainter>

namespace ss {

// Draws one caption cue at time t. Used by the viewer, the template gallery thumbnails
// and the export pipeline (identical code path => preview/export parity).
class CaptionRenderer {
public:
    struct SpeakerInfo {
        QString name;
        QColor color;
    };
    static void paintCue(QPainter &p, const QSize &frame, const CaptionStyle &style, const CueInstance &cue, TimeUs t,
                         const SpeakerInfo *speaker = nullptr);
    // Bounding rect of the cue block (for hit-testing / safe-area checks)
    static QRectF cueRect(const QSize &frame, const CaptionStyle &style, const CueInstance &cue);
    // The box actually drawn at time t (one-word-at-a-time styles show only the active word).
    static QRectF cueRect(const QSize &frame, const CaptionStyle &style, const CueInstance &cue, TimeUs t);
    static QFont fontFor(const CaptionStyle &style, int frameHeight);
};

class GraphicsRenderer {
public:
    static void paintTitle(QPainter &p, const QSize &frame, const Clip &clip, TimeUs t);
    static void paintShape(QPainter &p, const QSize &frame, const Clip &clip, TimeUs t);
    static double clipOpacity(const Clip &clip, TimeUs t); // fades * opacity
    // Un-animated bounds in frame pixels (for on-canvas selection, dragging and resizing).
    static QRectF titleRect(const QSize &frame, const Clip &clip);
    static QRectF shapeRect(const QSize &frame, const Clip &clip);
    static QFont titleFont(const TitleProps &tp, int frameHeight);
    static QStringList titleInAnimations();
    static QStringList titleOutAnimations();
    static QStringList titleLoopAnimations();
};

// A self-contained snapshot of everything drawn above the video layers: titles, shapes
// and burned-in captions. Built on the GUI thread, safe to use from the export thread.
class OverlayScene {
public:
    struct CaptionLayer {
        QString trackId;
        CaptionStyle style;
        QVector<CueInstance> cues;
        bool burnIn = true;
    };
    struct GraphicLayer {
        Clip clip;
    };

    static OverlayScene build(const Project &p, bool includeCaptions = true, bool onlyBurnIn = false);

    bool isEmptyAt(TimeUs t) const;
    bool isEmpty() const;
    void render(QPainter &p, const QSize &frame, TimeUs t) const;
    void renderCaptions(QPainter &p, const QSize &frame, TimeUs t) const;
    QImage renderImage(const QSize &frame, TimeUs t) const; // transparent ARGB32_Premultiplied

    QVector<CaptionLayer> captions;
    QVector<GraphicLayer> graphics; // in stacking order
    QVector<Speaker> speakers;
};

} // namespace ss
