#pragma once

#include "../captions/CaptionEngine.h"
#include "../core/Document.h"

#include <QAbstractScrollArea>
#include <QHash>

class QTimer;
class QMimeData;

namespace ss {

class MediaCache;

class TimelineWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    TimelineWidget(Document *doc, MediaCache *cache, QWidget *parent = nullptr);

    void setCompact(bool c);
    void setSnapping(bool on) { m_snap = on; }
    bool snapping() const { return m_snap; }
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void ensurePlayheadVisible(bool page);
    double pxPerSec() const { return m_pxPerSec; }

signals:
    void seekRequested(ss::TimeUs t);
    void clipActivated(const QString &clipId);       // double-click
    void cueActivated(const QString &trackId, int cue);
    // trackId may be TimelineOps::kNewTrack; insert = push existing clips aside (Ctrl/Cmd held)
    void filesDropped(const QStringList &paths, ss::TimeUs at, const QString &trackId, bool insert);
    // clipId "-" = dropped on empty space
    void libraryItemDropped(const QString &itemId, const QString &clipId, ss::TimeUs at, const QString &trackId);
    void statusMessage(const QString &msg);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    bool viewportEvent(QEvent *e) override;

private:
    struct Row {
        int trackIndex = -1;
        int y = 0; // content y (below ruler)
        int h = 0;
    };
    enum class Part { None, Body, EdgeStart, EdgeEnd, FadeIn, FadeOut };
    struct Hit {
        int row = -1;
        QString clipId;
        Part part = Part::None;
        int cueIndex = -1;
        bool cueEdgeStart = false, cueEdgeEnd = false;
        bool header = false;
        QString headerButton;
    };
    enum class Drag { None, Scrub, Move, TrimStart, TrimEnd, FadeIn, FadeOut, CueStart, CueEnd, CueMove, TrackReorder };
    // Something being dragged in from outside (media bin, Finder/Explorer, Library)
    struct DropGhost {
        TimeUs duration = 0;
        QString label;
        bool audio = false;
        QColor color;
    };

    void rebuild();
    void onPlayheadMoved();
    void updateScrollbars();
    int headerWidth() const { return m_compact ? 116 : 150; }
    int rulerHeight() const { return 28; }
    double xForTime(TimeUs t) const;
    TimeUs timeForX(double x) const;
    int rowAtY(int y) const;
    // +1: the "new track" lane above the top track, -1: the lane below the last track, 0: a track
    int laneAtY(int y) const;
    int topLaneHeight() const { return 14; }
    int bottomLaneHeight() const { return 36; }
    QRect laneRect(int lane) const;
    void paintDropFeedback(QPainter &p);
    void paintLaneHint(QPainter &p, int lane, bool active, const QString &text);
    void handleDragMove(const QPoint &pos, Qt::KeyboardModifiers mods);
    void updateMoveDrag(const QPoint &pos, Qt::KeyboardModifiers mods);
    void updateExternalDrag(const QPoint &pos, const QMimeData *mime, Qt::KeyboardModifiers mods);
    void autoScrollStep();
    void clearExternalDrag();
    TimeUs cueShiftDelta() const;
    QRect clipRect(const Row &row, const Clip &c) const;
    Hit hitTest(const QPoint &pos) const;
    QColor colorFor(const Clip &c) const;
    void paintRuler(QPainter &p);
    void paintRow(QPainter &p, const Row &row);
    void paintClip(QPainter &p, const Row &row, const Clip &c, const QRect &r, bool ghost);
    void paintHeader(QPainter &p, const Row &row);
    QStringList headerButtons(const Track &t) const;
    void toggleHeaderButton(int trackIndex, const QString &btn);
    TimeUs snapTime(TimeUs t, const QStringList &exclude, bool *snapped) const;
    void showClipMenu(const QPoint &global, const QString &clipId);
    void showTrackMenu(const QPoint &global, int trackIndex);
    void showEmptyMenu(const QPoint &global, TimeUs at, int trackIndex);

    Document *m_doc;
    MediaCache *m_cache;
    QVector<Row> m_rows;
    QHash<QString, QVector<CueInstance>> m_cues; // caption track id -> mapped cues
    double m_pxPerSec = 80;
    bool m_compact = false;
    int m_density = 0; // 0 normal, 1 compact, 2 dense, 3 tiny (chosen to fit the available height)
    bool m_snap = true;

    // drag state
    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    QString m_dragClip;
    QStringList m_dragIds;
    bool m_dragLinked = true;
    TimeUs m_dragDelta = 0;
    TimeUs m_dragValue = 0;
    QString m_dragTargetTrack;
    QString m_dragCueTrack;
    int m_dragCue = -1;
    bool m_dragMoved = false;
    TimeUs m_snapLine = -1;
    TimeUs m_dropTime = -1;
    int m_dropRow = -1;
    // drag-and-drop feedback
    int m_dragLane = 0;          // lane under the cursor while moving clips
    bool m_dragInsert = false;   // Ctrl/Cmd held: insert and push clips aside
    bool m_dragNewAbove = false; // target track busy there: the move makes a new track next to it
    bool m_dropNewAbove = false; // same for an external drop
    TimeUs m_insertAt = -1;      // where an insert will happen (for the marker)
    int m_dropLane = 0;          // lane under the cursor for an external drop
    bool m_dropInsert = false;
    QVector<DropGhost> m_dropGhost;
    QString m_dropTargetClip;    // clip a Library item will be applied to
    QString m_dropLibraryId;
    bool m_extDrag = false;
    QPoint m_lastDragPos;
    Qt::KeyboardModifiers m_lastMods;
    QTimer *m_autoScroll = nullptr;
    int m_reorderTrack = -1;     // track index being dragged by its header
    int m_reorderTo = -1;        // destination track index
    TimeUs m_cueOrigStart = 0, m_cueOrigEnd = 0;
    bool m_anyKeyframes = false;
    QHash<QString, QImage> m_thumbCache;
    int m_lastPlayheadX = -100000;
};

} // namespace ss
