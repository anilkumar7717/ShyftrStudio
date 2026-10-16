#pragma once

class QPlainTextEdit;

#include "../captions/OverlayRenderer.h"
#include "../core/Document.h"
#include "../media/CanvasLayout.h"

#include <QImage>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QToolButton;
class QComboBox;
class QLineEdit;

namespace ss {

class TextToolbar;

class PreviewEngine;

// The video frame plus direct manipulation of what is drawn on it: click to select a
// caption, title, shape or video layer, drag to move it (with snapping guides), drag a
// corner to resize. Titles and shapes can also be dropped here from the Effects panel.
class ViewerCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ViewerCanvas(Document *doc, QWidget *parent = nullptr);
    void setFrame(const QImage &img);
    QSize sizeHint() const override { return {640, 360}; }
    QRectF imageRect() const; // where the canvas is drawn, in widget coordinates
    QRect widgetRectFor(const QRectF &canvasRect) const;
    QPointF toCanvas(const QPointF &widgetPos) const;
    void clearCanvasSelection();
    // Selects the top-most item at a canvas position (used by tests / automation).
    bool selectAt(const QPointF &widgetPos);
    // Where a title/shape/video clip is drawn right now, in widget coordinates (invalid if not visible).
    QRect widgetRectForClip(const QString &clipId);

    static constexpr const char *kEffectMime = "application/x-shyftr-effect";

signals:
    void filesDropped(const QStringList &paths);
    void editRequested(const QString &titleClipId, const QRect &anchor); // empty id => caption at playhead
    void effectDropped(const QString &effectId, const QPointF &normalizedPos);
    void statusMessage(const QString &msg);
    void pauseRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool captionPerCue(Qt::KeyboardModifiers mods) const; // does a caption drag move only that caption?
    void keyPressEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    enum class Drag { None, Move, Resize };
    QVector<CanvasLayout::Item> items();
    const CanvasLayout::Item *itemAt(const QVector<CanvasLayout::Item> &list, const QPointF &widgetPos) const;
    bool findSelected(const QVector<CanvasLayout::Item> &list, CanvasLayout::Item *out) const;
    bool isSelected(const CanvasLayout::Item &it) const;
    void select(const CanvasLayout::Item &it);
    int cornerAt(const QRect &r, const QPointF &pos) const;
    double scaleK() const;
    QPointF snapDelta(const QRectF &startRect, QPointF d, bool resizing);
    void scheduleApply(const QString &description, const std::function<bool(Project &)> &fn);
    void flushApply();
    void updateCursor(const QPointF &pos);

    Document *m_doc;
    QImage m_frame;
    OverlayScene m_scene;
    bool m_sceneDirty = true;

    // selection (titles/shapes/video by clip id, captions by caption track)
    QString m_selClip;
    QString m_selCaptionTrack;
    int m_selCue = -1; // the caption clicked (selection follows it in "only that caption" mode)
    CanvasLayout::Item m_hover;
    bool m_hasHover = false;

    Drag m_drag = Drag::None;
    bool m_dragStarted = false;
    int m_corner = -1;
    QPointF m_pressPos;
    CanvasLayout::Grab m_grab;
    QRectF m_startRect;
    int m_serial = 0;
    QVector<double> m_guideX, m_guideY; // canvas coordinates of active snap guides

    std::function<bool(Project &)> m_pending;
    QString m_pendingDesc;
    QTimer m_applyTimer;

    QPointF m_dropPos{-1, -1};
};

// Viewer with transport controls.
class ViewerWidget : public QWidget {
    Q_OBJECT
public:
    ViewerWidget(Document *doc, PreviewEngine *engine, QWidget *parent = nullptr);
    ViewerCanvas *canvas() const { return m_canvas; }
    // Shows an in-place text editor over the video (over `anchor` when given).
    void startInlineEdit(const QString &text, const std::function<void(const QString &)> &commit, const QRect &anchor = QRect());
    // Multi-line editor for a text clip, styled like the text. `live` runs on every keystroke
    // (so the video updates as you type); `done(accepted)` runs once at the end.
    void startTextEdit(const QString &text, const QFont &font, const QColor &color,
                       const std::function<void(const QString &)> &live, const std::function<void(bool)> &done,
                       const QRect &anchor = QRect());
    bool isEditingText() const;
    // Opens the on-video editor for a text clip (emits titleEditRequested with where it is drawn).
    void requestTitleEdit(const QString &clipId);
    TextToolbar *textToolbar() const { return m_textBar; }
signals:
    void filesDropped(const QStringList &paths);
    void captionEditRequested(const QRect &anchor);
    void titleEditRequested(const QString &clipId, const QRect &anchor);
    void effectDropped(const QString &effectId, const QPointF &normalizedPos);
    void statusMessage(const QString &msg);
    void addTextRequested();

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void updateTime();
    void layoutEditor();
    void layoutTextBar();
    void finishTextEdit(bool accepted);
    QLineEdit *m_editor = nullptr;
    QPlainTextEdit *m_textEditor = nullptr;
    std::function<void(const QString &)> m_textLive;
    std::function<void(bool)> m_textDone;
    TextToolbar *m_textBar = nullptr;
    QRect m_editAnchor;
    std::function<void(const QString &)> m_commit;
    Document *m_doc;
    PreviewEngine *m_engine;
    ViewerCanvas *m_canvas;
    QLabel *m_time;
    QLabel *m_info;
    QToolButton *m_play;
};

} // namespace ss
