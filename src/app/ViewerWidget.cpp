#include "ViewerWidget.h"
#include <QTimer>
#include <QApplication>
#include <QPlainTextEdit>
#include "TextToolbar.h"
#include "../core/Settings.h"
#include "../core/TimelineOps.h"
#include "PreviewEngine.h"
#include "Theme.h"

#include <QComboBox>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace ss {

ViewerCanvas::ViewerCanvas(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(240, 135);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Video preview");
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(16); // at most ~60 model updates per second while dragging
    connect(&m_applyTimer, &QTimer::timeout, this, &ViewerCanvas::flushApply);
    connect(doc, &Document::projectChanged, this, [this]() {
        m_sceneDirty = true;
        update();
    });
    connect(doc, &Document::projectReset, this, [this]() {
        m_sceneDirty = true;
        m_selClip.clear();
        m_selCaptionTrack.clear();
        m_hasHover = false;
        update();
    });
    connect(doc, &Document::selectionChanged, this, [this]() {
        const QStringList sel = m_doc->selectedClips();
        if (sel.size() == 1) {
            m_selClip = sel.first();
            m_selCaptionTrack.clear();
        } else if (!m_selClip.isEmpty()) {
            m_selClip.clear();
        }
        update();
    });
    connect(doc, &Document::playheadChanged, this, [this]() { update(); });
}

void ViewerCanvas::setFrame(const QImage &img)
{
    m_frame = img;
    update();
}

QRectF ViewerCanvas::imageRect() const
{
    QSize src = m_frame.size();
    if (m_doc->isOpen())
        src = QSize(m_doc->project().canvas.width, m_doc->project().canvas.height);
    if (src.isEmpty())
        return QRectF(rect());
    const QSize s = src.scaled(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0), QSizeF(s));
}

double ViewerCanvas::scaleK() const
{
    const int cw = m_doc->isOpen() ? m_doc->project().canvas.width : std::max(1, m_frame.width());
    return imageRect().width() / std::max(1, cw);
}

QRect ViewerCanvas::widgetRectFor(const QRectF &c) const
{
    const QRectF r = imageRect();
    const double k = scaleK();
    return QRectF(r.left() + c.left() * k, r.top() + c.top() * k, c.width() * k, c.height() * k).toAlignedRect();
}

QPointF ViewerCanvas::toCanvas(const QPointF &w) const
{
    const QRectF r = imageRect();
    const double k = std::max(1e-6, scaleK());
    return QPointF((w.x() - r.left()) / k, (w.y() - r.top()) / k);
}

QVector<CanvasLayout::Item> ViewerCanvas::items()
{
    if (!m_doc->isOpen())
        return {};
    if (m_sceneDirty) {
        m_scene = OverlayScene::build(m_doc->project());
        m_sceneDirty = false;
    }
    return CanvasLayout::itemsAt(m_doc->project(), m_scene, m_doc->playhead());
}

const CanvasLayout::Item *ViewerCanvas::itemAt(const QVector<CanvasLayout::Item> &list, const QPointF &pos) const
{
    const QPointF c = toCanvas(pos);
    const double slop = 4 / std::max(1e-6, scaleK()); // a few pixels of tolerance around thin items
    for (const auto &it : list)
        if (it.rect.adjusted(-slop, -slop, slop, slop).contains(c))
            return &it;
    return nullptr;
}

QRect ViewerCanvas::widgetRectForClip(const QString &clipId)
{
    for (const auto &it : items())
        if (it.clipId == clipId)
            return widgetRectFor(it.rect);
    return {};
}

bool ViewerCanvas::isSelected(const CanvasLayout::Item &it) const
{
    if (it.kind == CanvasLayout::Item::Caption)
        return !m_selCaptionTrack.isEmpty() && it.trackId == m_selCaptionTrack
               && (Settings::instance().captionDragAll || it.cueIndex == m_selCue);
    return !m_selClip.isEmpty() && it.clipId == m_selClip;
}

bool ViewerCanvas::findSelected(const QVector<CanvasLayout::Item> &list, CanvasLayout::Item *out) const
{
    for (const auto &it : list)
        if (isSelected(it)) {
            *out = it;
            return true;
        }
    return false;
}

void ViewerCanvas::clearCanvasSelection()
{
    m_selCaptionTrack.clear();
    m_selClip.clear();
    update();
}

void ViewerCanvas::select(const CanvasLayout::Item &it)
{
    using K = CanvasLayout::Item;
    if (it.kind == K::Caption) {
        m_selCaptionTrack = it.trackId;
        m_selCue = it.cueIndex;
        m_selClip.clear();
        if (!m_doc->selectedClips().isEmpty())
            m_doc->setSelectedClips({}); // so Delete never removes a clip selected earlier
        m_doc->setSelectedCue(it.trackId, it.cueIndex);
        emit statusMessage(Settings::instance().captionDragAll
                               ? "Caption selected — drag moves ALL captions · Alt/Option+drag moves only this one · right-click to change"
                               : "Caption selected — drag moves only THIS caption · Alt/Option+drag moves all captions · drag a corner to resize · double-click to edit");
    } else {
        m_selCaptionTrack.clear();
        m_selClip = it.clipId;
        m_doc->setSelectedClips({it.clipId});
        if (it.kind == K::Title)
            emit statusMessage("Title selected — drag to move · drag a corner to resize · double-click to edit the text");
        else if (it.kind == K::Shape)
            emit statusMessage("Shape selected — drag to move · drag a corner to resize (Shift keeps proportions)");
        else
            emit statusMessage("Video selected — drag to reposition · drag a corner to scale · right-click to reset");
    }
    update();
}

bool ViewerCanvas::selectAt(const QPointF &pos)
{
    const auto list = items();
    if (const auto *it = itemAt(list, pos)) {
        select(*it);
        return true;
    }
    clearCanvasSelection();
    return false;
}

int ViewerCanvas::cornerAt(const QRect &r, const QPointF &pos) const
{
    const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    for (int i = 0; i < 4; ++i)
        if (std::abs(pos.x() - corners[i].x()) <= 8 && std::abs(pos.y() - corners[i].y()) <= 8)
            return i;
    return -1;
}

void ViewerCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#0B0C0F"));
    if (m_frame.isNull()) {
        p.setPen(Theme::textDim());
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, "Drop video, audio or images here\nor use File → Import Media");
        return;
    }
    const QRectF r = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_frame);
    if (!m_doc->isOpen())
        return;

    const QColor accent("#5B8CFF");
    const auto list = items();
    p.setRenderHint(QPainter::Antialiasing);

    // hover outline
    if (m_hasHover && m_drag == Drag::None) {
        for (const auto &it : list)
            if (it.sameAs(m_hover) && !isSelected(it)) {
                p.setPen(QPen(QColor(255, 255, 255, 170), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(QRectF(widgetRectFor(it.rect)).adjusted(-2.5, -2.5, 2.5, 2.5));
            }
    }

    // snapping guides
    p.setPen(QPen(QColor("#FF4FD8"), 1));
    const double k = scaleK();
    for (double gx : m_guideX)
        p.drawLine(QPointF(r.left() + gx * k, r.top()), QPointF(r.left() + gx * k, r.bottom()));
    for (double gy : m_guideY)
        p.drawLine(QPointF(r.left(), r.top() + gy * k), QPointF(r.right(), r.top() + gy * k));

    // selection box, handles and a small label
    CanvasLayout::Item sel;
    if (findSelected(list, &sel)) {
        const QRectF wr = QRectF(widgetRectFor(sel.rect)).adjusted(-3, -3, 3, 3);
        p.setPen(QPen(sel.locked ? QColor("#8a8f99") : accent, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(wr);
        if (!sel.locked) {
            p.setPen(QPen(accent, 1.5));
            p.setBrush(Qt::white);
            for (const QPointF &c : {wr.topLeft(), wr.topRight(), wr.bottomRight(), wr.bottomLeft()})
                p.drawRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9));
        }
        QString label = sel.kind == CanvasLayout::Item::Caption ? (sel.cueHasOwnPosition || (m_drag != Drag::None && m_grab.perCue)
                                                                        ? "This caption"
                                                                        : "All captions")
                        : sel.kind == CanvasLayout::Item::Title ? "Title"
                        : sel.kind == CanvasLayout::Item::Shape ? "Shape"
                                                                : "Video";
        if (sel.locked)
            label += " · locked";
        QFont f = font();
        f.setPointSizeF(std::max(8.0, f.pointSizeF() - 1));
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        QRectF chip(wr.left(), wr.top() - fm.height() - 8, fm.horizontalAdvance(label) + 14, fm.height() + 4);
        if (chip.top() < r.top())
            chip.moveTop(wr.top() + 6);
        chip.moveLeft(std::clamp(chip.left(), r.left(), std::max(r.left(), r.right() - chip.width())));
        p.setPen(Qt::NoPen);
        p.setBrush(sel.locked ? QColor("#8a8f99") : accent);
        p.drawRoundedRect(chip, 4, 4);
        p.setPen(Qt::white);
        p.drawText(chip, Qt::AlignCenter, label);
    }

    // drop target marker while dragging a title/shape from the Effects panel
    if (m_dropPos.x() >= 0) {
        p.setPen(QPen(accent, 2));
        p.setBrush(QColor(91, 140, 255, 60));
        p.drawEllipse(m_dropPos, 14, 14);
        p.drawLine(m_dropPos - QPointF(22, 0), m_dropPos + QPointF(22, 0));
        p.drawLine(m_dropPos - QPointF(0, 22), m_dropPos + QPointF(0, 22));
    }
}

void ViewerCanvas::updateCursor(const QPointF &pos)
{
    if (!m_doc->isOpen() || m_frame.isNull()) {
        unsetCursor();
        return;
    }
    const auto list = items();
    CanvasLayout::Item sel;
    if (findSelected(list, &sel) && !sel.locked) {
        const int c = cornerAt(widgetRectFor(sel.rect).adjusted(-3, -3, 3, 3), pos);
        if (c >= 0) {
            setCursor(c % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            return;
        }
    }
    const auto *it = itemAt(list, pos);
    const bool hadHover = m_hasHover;
    const CanvasLayout::Item oldHover = m_hover;
    m_hasHover = it != nullptr;
    if (it)
        m_hover = *it;
    if (hadHover != m_hasHover || (it && !oldHover.sameAs(*it)))
        update();
    if (!it || it->locked)
        unsetCursor();
    else if (it->kind == CanvasLayout::Item::Media && !isSelected(*it))
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::SizeAllCursor);
}

void ViewerCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_doc->isOpen() || m_frame.isNull())
        return QWidget::mousePressEvent(e);
    setFocus();
    m_pressPos = e->position();
    m_drag = Drag::None;
    m_dragStarted = false;
    const auto list = items();

    // corner handle of the current selection?
    CanvasLayout::Item sel;
    if (findSelected(list, &sel) && !sel.locked) {
        const int c = cornerAt(widgetRectFor(sel.rect).adjusted(-3, -3, 3, 3), e->position());
        if (c >= 0) {
            emit pauseRequested();
            m_corner = c;
            m_drag = Drag::Resize;
            m_grab = CanvasLayout::grab(m_doc->project(), sel, captionPerCue(e->modifiers()));
            m_startRect = sel.rect;
            ++m_serial;
            return;
        }
    }
    const auto *it = itemAt(list, e->position());
    if (!it) {
        clearCanvasSelection();
        if (!m_doc->selectedClips().isEmpty())
            m_doc->setSelectedClips({});
        return;
    }
    const bool wasSelected = isSelected(*it);
    select(*it);
    if (it->locked) {
        emit statusMessage("That track is locked — unlock it in the timeline (L button) to move this");
        return;
    }
    // The main video usually fills the frame: require a first click to select it so a stray
    // click on the picture never moves it by accident.
    if (it->kind == CanvasLayout::Item::Media && !wasSelected)
        return;
    emit pauseRequested();
    m_drag = Drag::Move;
    m_grab = CanvasLayout::grab(m_doc->project(), *it, captionPerCue(e->modifiers()));
    m_startRect = it->rect;
    ++m_serial;
}

QPointF ViewerCanvas::snapDelta(const QRectF &start, QPointF d, bool)
{
    m_guideX.clear();
    m_guideY.clear();
    const Project &p = m_doc->project();
    const double W = p.canvas.width, H = p.canvas.height;
    const QRectF safe = SafeArea::forFrame(p.canvas.width, p.canvas.height);
    const double thr = 7.0 / std::max(1e-6, scaleK());
    auto best = [thr](const QVector<double> &edges, const QVector<double> &targets, double *adj, double *guide) {
        double bestDist = thr + 1;
        for (double e : edges)
            for (double t : targets)
                if (std::abs(t - e) < bestDist) {
                    bestDist = std::abs(t - e);
                    *adj = t - e;
                    *guide = t;
                }
        return bestDist <= thr;
    };
    const QRectF c = start.translated(d);
    double adj = 0, guide = 0;
    if (best({c.left(), c.center().x(), c.right()}, {0, W / 2, W, safe.left(), safe.right()}, &adj, &guide)) {
        d.rx() += adj;
        m_guideX << guide;
    }
    if (best({c.top(), c.center().y(), c.bottom()}, {0, H / 2, H, safe.top(), safe.bottom()}, &adj, &guide)) {
        d.ry() += adj;
        m_guideY << guide;
    }
    return d;
}

void ViewerCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) {
        updateCursor(e->position());
        return QWidget::mouseMoveEvent(e);
    }
    if (!m_dragStarted && (e->position() - m_pressPos).manhattanLength() < 3)
        return;
    m_dragStarted = true;
    const Project &p = m_doc->project();
    const double W = p.canvas.width, H = p.canvas.height;
    const CanvasLayout::Grab g = m_grab;

    if (m_drag == Drag::Move) {
        QPointF d = toCanvas(e->position()) - toCanvas(m_pressPos);
        if (e->modifiers() & Qt::ShiftModifier) { // constrain to one axis
            if (std::abs(d.x()) > std::abs(d.y()))
                d.setY(0);
            else
                d.setX(0);
        }
        if (e->modifiers() & Qt::ControlModifier) {
            m_guideX.clear();
            m_guideY.clear();
        } else {
            d = snapDelta(m_startRect, d, false);
        }
        if (g.item.kind != CanvasLayout::Item::Media) { // keep overlays on screen
            if (m_startRect.width() <= W)
                d.setX(std::clamp(d.x(), -m_startRect.left(), W - m_startRect.right()));
            if (m_startRect.height() <= H)
                d.setY(std::clamp(d.y(), -m_startRect.top(), H - m_startRect.bottom()));
        }
        const QString what = g.item.kind == CanvasLayout::Item::Caption ? (g.perCue ? "Move caption" : "Move captions")
                             : g.item.kind == CanvasLayout::Item::Title ? "Move title"
                             : g.item.kind == CanvasLayout::Item::Shape ? "Move shape"
                                                                        : "Move video";
        scheduleApply(what, [g, d](Project &pr) { return CanvasLayout::move(pr, g, d); });
    } else {
        const QPointF pt = toCanvas(e->position());
        if (g.item.kind == CanvasLayout::Item::Shape) {
            const QPointF corners[4] = {m_startRect.topLeft(), m_startRect.topRight(), m_startRect.bottomRight(),
                                        m_startRect.bottomLeft()};
            const QPointF fixed = corners[(m_corner + 2) % 4];
            QRectF nr = QRectF(fixed, pt).normalized();
            if (e->modifiers() & Qt::ShiftModifier && m_startRect.height() > 0) {
                const double ratio = m_startRect.width() / m_startRect.height();
                if (nr.width() / std::max(1.0, nr.height()) > ratio)
                    nr.setWidth(nr.height() * ratio);
                else
                    nr.setHeight(nr.width() / ratio);
                if (pt.x() < fixed.x())
                    nr.moveRight(fixed.x());
                if (pt.y() < fixed.y())
                    nr.moveBottom(fixed.y());
            }
            nr.setWidth(std::max(4.0, nr.width()));
            nr.setHeight(std::max(2.0, nr.height()));
            scheduleApply("Resize shape", [g, nr](Project &pr) { return CanvasLayout::setShapeRect(pr, g, nr); });
        } else {
            const QPointF c = m_startRect.center();
            const double d0 = std::max(4.0, QLineF(c, toCanvas(m_pressPos)).length());
            const double factor = QLineF(c, pt).length() / d0;
            const QString what = g.item.kind == CanvasLayout::Item::Caption ? (g.perCue ? "Resize caption" : "Resize captions")
                                 : g.item.kind == CanvasLayout::Item::Title ? "Resize title"
                                                                            : "Scale video";
            scheduleApply(what, [g, factor](Project &pr) { return CanvasLayout::scale(pr, g, factor); });
        }
    }
    update();
}

void ViewerCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag != Drag::None) {
        flushApply();
        if (m_dragStarted)
            emit statusMessage(m_drag == Drag::Move ? "Moved — ⌘Z / Ctrl+Z to undo" : "Resized — ⌘Z / Ctrl+Z to undo");
    }
    m_drag = Drag::None;
    m_dragStarted = false;
    m_guideX.clear();
    m_guideY.clear();
    update();
    QWidget::mouseReleaseEvent(e);
}

void ViewerCanvas::scheduleApply(const QString &description, const std::function<bool(Project &)> &fn)
{
    m_pending = fn;
    m_pendingDesc = description;
    if (!m_applyTimer.isActive())
        m_applyTimer.start();
}

void ViewerCanvas::flushApply()
{
    m_applyTimer.stop();
    if (!m_pending)
        return;
    auto fn = std::move(m_pending);
    m_pending = nullptr;
    m_doc->apply(m_pendingDesc, fn, QString("canvas-drag-%1").arg(m_serial));
}

void ViewerCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const auto list = items();
    const auto *it = itemAt(list, e->position());
    if (it && it->kind == CanvasLayout::Item::Title)
        emit editRequested(it->clipId, widgetRectFor(it->rect));
    else if (it && it->kind == CanvasLayout::Item::Caption)
        emit editRequested(QString(), widgetRectFor(it->rect));
    else
        emit editRequested(QString(), QRect());
}

bool ViewerCanvas::captionPerCue(Qt::KeyboardModifiers mods) const
{
    const bool alt = mods & Qt::AltModifier;
    return Settings::instance().captionDragAll ? alt : !alt;
}

void ViewerCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_doc->isOpen())
        return;
    const auto list = items();
    const auto *hit = itemAt(list, e->pos());
    if (!hit)
        return;
    const CanvasLayout::Item it = *hit;
    select(it);
    using K = CanvasLayout::Item;
    QMenu menu(this);
    if (it.kind == K::Caption || it.kind == K::Title) {
        menu.addAction(it.kind == K::Caption ? "Edit caption text" : "Edit title text", this, [this, it]() {
            emit editRequested(it.kind == K::Title ? it.clipId : QString(), widgetRectFor(it.rect));
        });
        menu.addSeparator();
    }
    if (it.kind == K::Caption) {
        // what a plain drag on a caption does (remembered); Alt/Option does the other one
        QMenu *mode = menu.addMenu("Dragging a caption moves");
        QAction *one = mode->addAction("Only that caption");
        QAction *all = mode->addAction("All captions together");
        one->setCheckable(true);
        all->setCheckable(true);
        (Settings::instance().captionDragAll ? all : one)->setChecked(true);
        connect(one, &QAction::triggered, this, []() {
            Settings::instance().captionDragAll = false;
            Settings::instance().save();
        });
        connect(all, &QAction::triggered, this, []() {
            Settings::instance().captionDragAll = true;
            Settings::instance().save();
        });
        menu.addSeparator();
        QAction *rp = menu.addAction("Reset this caption's position");
        rp->setEnabled(it.cueHasOwnPosition);
        connect(rp, &QAction::triggered, this, [this, it]() {
            m_doc->apply("Reset caption position", [it](Project &p) { return CanvasLayout::resetPosition(p, it, true); });
        });
        QAction *same = menu.addAction("Put all captions at this position");
        connect(same, &QAction::triggered, this, [this, it]() {
            m_doc->apply("Caption position for all", [it](Project &p) { return CanvasLayout::applyCaptionPositionToAll(p, it); });
        });
        menu.addAction("Reset all caption positions", this, [this, it]() {
            m_doc->apply("Reset caption positions", [it](Project &p) { return CanvasLayout::resetPosition(p, it, false); });
        });
        menu.addAction("Reset this caption's size", this, [this, it]() {
            m_doc->apply("Reset caption size", [it](Project &p) { return CanvasLayout::resetSize(p, it, true); });
        });
        menu.addAction("Reset all caption sizes", this, [this, it]() {
            m_doc->apply("Reset caption sizes", [it](Project &p) { return CanvasLayout::resetSize(p, it, false); });
        });
    } else {
        QAction *resetPos = menu.addAction("Reset position (centre)");
        connect(resetPos, &QAction::triggered, this, [this, it]() {
            m_doc->apply("Reset position", [it](Project &p) { return CanvasLayout::resetPosition(p, it); });
        });
        if (it.kind != K::Shape) {
            QAction *resetSize = menu.addAction(it.kind == K::Media ? "Reset scale (100%)" : "Reset size");
            connect(resetSize, &QAction::triggered, this, [this, it]() {
                m_doc->apply("Reset size", [it](Project &p) { return CanvasLayout::resetSize(p, it); });
            });
        }
    }
    if (it.kind == K::Title || it.kind == K::Shape) {
        menu.addSeparator();
        menu.addAction(it.kind == K::Title ? "Delete title" : "Delete shape", this, [this, it]() {
            m_doc->apply("Delete", [it](Project &p) { return TimelineOps::deleteClips(p, {it.clipId}, false); });
            clearCanvasSelection();
        });
    }
    for (QAction *a : menu.actions())
        a->setEnabled(a->isSeparator() || !it.locked);
    menu.exec(e->globalPos());
}

void ViewerCanvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && (!m_selClip.isEmpty() || !m_selCaptionTrack.isEmpty())) {
        clearCanvasSelection();
        return;
    }
    QWidget::keyPressEvent(e);
}

void ViewerCanvas::leaveEvent(QEvent *e)
{
    if (m_hasHover) {
        m_hasHover = false;
        update();
    }
    QWidget::leaveEvent(e);
}

void ViewerCanvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() || (e->mimeData()->hasFormat(kEffectMime) && m_doc->isOpen()))
        e->acceptProposedAction();
}

void ViewerCanvas::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasFormat(kEffectMime)) {
        m_dropPos = e->position();
        update();
    }
    e->acceptProposedAction();
}

void ViewerCanvas::dragLeaveEvent(QDragLeaveEvent *e)
{
    m_dropPos = QPointF(-1, -1);
    update();
    QWidget::dragLeaveEvent(e);
}

void ViewerCanvas::dropEvent(QDropEvent *e)
{
    m_dropPos = QPointF(-1, -1);
    update();
    if (e->mimeData()->hasFormat(kEffectMime)) {
        const QString id = QString::fromUtf8(e->mimeData()->data(kEffectMime));
        const QRectF r = imageRect();
        const QPointF n(std::clamp((e->position().x() - r.left()) / std::max(1.0, r.width()), 0.0, 1.0),
                        std::clamp((e->position().y() - r.top()) / std::max(1.0, r.height()), 0.0, 1.0));
        e->acceptProposedAction();
        if (id.startsWith("textstyle:")) {
            // dropped on a text: style that text (and only that one); elsewhere: a new text there
            const auto list = items();
            const auto *it = itemAt(list, e->position());
            if (it && it->kind == CanvasLayout::Item::Title) {
                select(*it);
                emit effectDropped(id, QPointF(-1, -1));
            } else {
                m_doc->setSelectedClips({});
                emit effectDropped(id, n);
            }
            return;
        }
        emit effectDropped(id, n);
        return;
    }
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (!paths.isEmpty())
        emit filesDropped(paths);
}

ViewerWidget::ViewerWidget(Document *doc, PreviewEngine *engine, QWidget *parent)
    : QWidget(parent), m_doc(doc), m_engine(engine)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_canvas = new ViewerCanvas(doc);
    v->addWidget(m_canvas, 1);

    auto *bar = new QWidget;
    bar->setStyleSheet(QString("background:%1;").arg(Theme::panel().name()));
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 4, 8, 4);
    auto mk = [&](QStyle::StandardPixmap icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setIcon(style()->standardIcon(icon));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setAutoRaise(true);
        h->addWidget(b);
        return b;
    };
    auto *start = mk(QStyle::SP_MediaSkipBackward, "Go to start (Home)");
    auto *back = mk(QStyle::SP_MediaSeekBackward, "Previous frame (←)");
    m_play = mk(QStyle::SP_MediaPlay, "Play / Pause (Space)");
    auto *fwd = mk(QStyle::SP_MediaSeekForward, "Next frame (→)");
    auto *end = mk(QStyle::SP_MediaSkipForward, "Go to end (End)");
    m_time = new QLabel("00:00:00:00");
    m_time->setStyleSheet("font-family: Menlo, Consolas, monospace; font-size: 13pt; padding: 0 10px;");
    m_time->setAccessibleName("Current time");
    h->addWidget(m_time);
    h->addStretch(1);
    m_info = new QLabel;
    m_info->setObjectName("hint");
    h->addWidget(m_info);
    auto *addText = new QToolButton;
    addText->setText("+ Text");
    addText->setToolTip("Add text at the playhead and type it on the video (T)");
    addText->setAccessibleName("Add text");
    h->addWidget(addText);
    connect(addText, &QToolButton::clicked, this, &ViewerWidget::addTextRequested);
    auto *safe = new QToolButton;
    safe->setText("Safe area");
    safe->setCheckable(true);
    safe->setToolTip("Show caption-safe area guides");
    h->addWidget(safe);
    auto *quality = new QComboBox;
    quality->addItem("Preview: Full", 1920);
    quality->addItem("Preview: Half", 960);
    quality->addItem("Preview: Quarter", 480);
    quality->setToolTip("Lower preview resolution for smoother playback on slower computers");
    const int pref = Settings::instance().previewMaxWidth;
    quality->setCurrentIndex(pref >= 1920 ? 0 : pref >= 960 ? 1 : 2);
    h->addWidget(quality);
    v->addWidget(bar);

    connect(engine, &PreviewEngine::frameReady, m_canvas, &ViewerCanvas::setFrame);
    connect(engine, &PreviewEngine::playStateChanged, this, [this](bool playing) {
        m_play->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(m_play, &QToolButton::clicked, engine, &PreviewEngine::togglePlay);

    // formatting bar for the one selected text
    m_textBar = new TextToolbar(doc, m_canvas);
    auto syncTextBar = [this]() {
        const QStringList sel = m_doc->selectedClips();
        const Clip *c = sel.size() == 1 ? m_doc->project().findClip(sel.first()) : nullptr;
        if (c && c->type == ClipType::Title && !isEditingText()) {
            if (m_textBar->clipId() != c->id)
                m_textBar->setClip(c->id);
            m_textBar->show();
            m_textBar->raise();
            layoutTextBar();
        } else {
            m_textBar->hide();
            if (!c)
                m_textBar->setClip({});
        }
    };
    connect(doc, &Document::selectionChanged, this, syncTextBar);
    connect(doc, &Document::projectChanged, this, [this, syncTextBar]() {
        syncTextBar();
        QTimer::singleShot(0, this, [this]() { layoutTextBar(); });
    });
    connect(doc, &Document::playheadChanged, this, [this]() {
        if (m_textBar->isVisible())
            layoutTextBar();
    });
    connect(m_textBar, &TextToolbar::editTextRequested, this, &ViewerWidget::requestTitleEdit);
    connect(back, &QToolButton::clicked, this, [this]() { m_engine->stepFrames(-1); });
    connect(fwd, &QToolButton::clicked, this, [this]() { m_engine->stepFrames(1); });
    connect(start, &QToolButton::clicked, this, [this]() { m_engine->seek(0); });
    connect(end, &QToolButton::clicked, this, [this]() { m_engine->seek(m_doc->project().duration()); });
    connect(safe, &QToolButton::toggled, engine, &PreviewEngine::setShowSafeArea);
    connect(quality, &QComboBox::currentIndexChanged, this, [this, quality](int) {
        m_engine->setMaxPreviewWidth(quality->currentData().toInt());
        Settings::instance().previewMaxWidth = quality->currentData().toInt();
        Settings::instance().save();
    });
    connect(doc, &Document::playheadChanged, this, &ViewerWidget::updateTime);
    connect(doc, &Document::projectChanged, this, &ViewerWidget::updateTime);
    connect(m_canvas, &ViewerCanvas::filesDropped, this, &ViewerWidget::filesDropped);
    connect(m_canvas, &ViewerCanvas::editRequested, this, [this](const QString &titleId, const QRect &anchor) {
        if (titleId.isEmpty())
            emit captionEditRequested(anchor);
        else
            emit titleEditRequested(titleId, anchor);
    });
    connect(m_canvas, &ViewerCanvas::effectDropped, this, &ViewerWidget::effectDropped);
    connect(m_canvas, &ViewerCanvas::statusMessage, this, &ViewerWidget::statusMessage);
    connect(m_canvas, &ViewerCanvas::pauseRequested, m_engine, &PreviewEngine::pause);
    m_info->setMinimumWidth(0);
    m_info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    engine->setMaxPreviewWidth(quality->currentData().toInt());
    updateTime();
}

void ViewerWidget::startInlineEdit(const QString &text, const std::function<void(const QString &)> &commit, const QRect &anchor)
{
    m_editAnchor = anchor;
    if (!m_editor) {
        m_editor = new QLineEdit(m_canvas);
        m_editor->setAlignment(Qt::AlignCenter);
        m_editor->setStyleSheet("QLineEdit { background: rgba(10,12,16,230); color: white; border: 2px solid #5B8CFF;"
                                " border-radius: 8px; padding: 6px 10px; font-size: 15pt; font-weight: 600; }");
        m_editor->setAccessibleName("Edit caption text");
        m_editor->installEventFilter(this);
        connect(m_editor, &QLineEdit::returnPressed, this, [this]() {
            const QString t = m_editor->text();
            m_editor->hide();
            if (m_commit)
                m_commit(t);
            m_commit = nullptr;
            m_canvas->setFocus();
        });
    }
    m_commit = commit;
    m_editor->setText(text);
    m_editor->setPlaceholderText("Type the caption, Enter to save, Esc to cancel");
    layoutEditor();
    m_editor->show();
    m_editor->raise();
    m_editor->setFocus();
    m_editor->selectAll();
}

bool ViewerWidget::isEditingText() const
{
    return m_textEditor && m_textEditor->isVisible();
}

void ViewerWidget::requestTitleEdit(const QString &clipId)
{
    const Clip *c = m_doc->project().findClip(clipId);
    if (!c || c->type != ClipType::Title)
        return;
    // make sure the text is fully on screen (past its entry animation) before editing it
    const TimeUs settled = c->title.animation == "none" ? 0 : c->title.animationDuration;
    const TimeUs ph = m_doc->playhead();
    if (!c->containsTimeline(ph) || ph < c->start + settled)
        m_doc->setPlayhead(c->start + std::min<TimeUs>(std::max<TimeUs>(settled, 0) + frameDurationUs(m_doc->project().canvas.fps),
                                                       c->duration() / 2));
    emit titleEditRequested(clipId, m_canvas->widgetRectForClip(clipId));
}

void ViewerWidget::layoutTextBar()
{
    if (!m_textBar || !m_textBar->isVisible())
        return;
    m_textBar->adjustSize();
    const QSize sz = m_textBar->sizeHint();
    const int w = std::min(sz.width(), m_canvas->width() - 8);
    const QRect img = m_canvas->imageRect().toRect();
    const QRect text = m_canvas->widgetRectForClip(m_textBar->clipId());
    int y = std::max(4, img.top() + 6);
    // keep out of the way of the text being formatted: go below it when it sits at the top
    if (text.isValid() && text.top() < y + sz.height() + 6)
        y = std::min(m_canvas->height() - sz.height() - 4, std::max(text.bottom() + 8, img.bottom() - sz.height() - 6));
    m_textBar->setGeometry((m_canvas->width() - w) / 2, y, w, sz.height());
}

void ViewerWidget::startTextEdit(const QString &text, const QFont &font, const QColor &color,
                                 const std::function<void(const QString &)> &live, const std::function<void(bool)> &done,
                                 const QRect &anchor)
{
    if (isEditingText())
        finishTextEdit(true);
    m_editAnchor = anchor;
    if (!m_textEditor) {
        m_textEditor = new QPlainTextEdit(m_canvas);
        m_textEditor->setAccessibleName("Edit text");
        m_textEditor->setTabChangesFocus(true);
        m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->installEventFilter(this);
        connect(m_textEditor, &QPlainTextEdit::textChanged, this, [this]() {
            if (m_textLive && m_textEditor->isVisible())
                m_textLive(m_textEditor->toPlainText());
            layoutEditor();
        });
    }
    m_textLive = nullptr;
    m_textDone = done;
    QFont f = font;
    f.setPointSizeF(std::clamp(f.pointSizeF() > 0 ? f.pointSizeF() : 18.0, 14.0, 26.0));
    m_textEditor->setFont(f);
    const QColor fg = color.isValid() && color.alpha() > 0 ? color : QColor(Qt::white);
    const bool dark = fg.lightnessF() < 0.45;
    m_textEditor->setStyleSheet(QString("QPlainTextEdit { background: %1; color: %2; border: 2px solid #5B8CFF;"
                                        " border-radius: 8px; padding: 4px 8px; }")
                                    .arg(dark ? "rgba(245,246,250,250)" : "rgba(10,12,16,248)", fg.name()));
    m_textEditor->setPlainText(text);
    m_textEditor->setPlaceholderText("Type your text · Enter to finish · Shift+Enter for a new line · *stars* highlight words");
    m_textLive = live;
    if (m_textBar)
        m_textBar->hide();
    m_textEditor->show();
    layoutEditor(); // sized/placed over the text once visible
    m_textEditor->raise();
    m_textEditor->setFocus();
    m_textEditor->selectAll();
    emit statusMessage("Type your text — Enter to finish, Shift+Enter for a new line, Esc to cancel. *Stars* around words colour them.");
}

void ViewerWidget::finishTextEdit(bool accepted)
{
    if (!m_textEditor || !m_textEditor->isVisible())
        return;
    m_textLive = nullptr;
    m_textEditor->hide();
    auto done = std::move(m_textDone);
    m_textDone = nullptr;
    if (done)
        done(accepted);
    m_canvas->setFocus();
    // bring the formatting bar back for the text that is still selected
    emit m_doc->selectionChanged();
}

void ViewerWidget::layoutEditor()
{
    if (m_textEditor && m_textEditor->isVisible()) {
        const QFontMetrics fm(m_textEditor->font());
        const QStringList lines = m_textEditor->toPlainText().split('\n');
        int textW = 160;
        for (const QString &l : lines)
            textW = std::max(textW, fm.horizontalAdvance(l) + 40);
        const int w = std::clamp(std::max(textW, m_editAnchor.isValid() ? m_editAnchor.width() + 40 : 0), 240,
                                 std::max(240, m_canvas->width() - 16));
        const int h = std::min(m_canvas->height() - 16, fm.lineSpacing() * std::max(1, int(lines.size())) + 22);
        QPoint c = m_editAnchor.isValid() ? m_editAnchor.center() : QPoint(m_canvas->width() / 2, int(m_canvas->height() * 0.5));
        const int x = std::clamp(c.x() - w / 2, 8, std::max(8, m_canvas->width() - w - 8));
        const int y = std::clamp(c.y() - h / 2, 8, std::max(8, m_canvas->height() - h - 8));
        m_textEditor->setGeometry(x, y, w, h);
    }
    if (!m_editor)
        return;
    const int h = m_editor->sizeHint().height() + 6;
    if (m_editAnchor.isValid()) {
        // sit right on top of the caption/title being edited
        const int w = std::clamp(m_editAnchor.width() + 48, 240, std::max(240, m_canvas->width() - 16));
        int x = m_editAnchor.center().x() - w / 2;
        int y = m_editAnchor.center().y() - h / 2;
        x = std::clamp(x, 8, std::max(8, m_canvas->width() - w - 8));
        y = std::clamp(y, 8, std::max(8, m_canvas->height() - h - 8));
        m_editor->setGeometry(x, y, w, h);
        return;
    }
    const int w = std::max(200, int(m_canvas->width() * 0.86));
    m_editor->setGeometry((m_canvas->width() - w) / 2, int(m_canvas->height() * 0.78) - h / 2, w, h);
}

void ViewerWidget::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutEditor();
    layoutTextBar();
    // hide secondary info on narrow viewers
    m_info->setVisible(width() > 720);
}

bool ViewerWidget::eventFilter(QObject *o, QEvent *e)
{
    if (m_textEditor && o == m_textEditor) {
        // Esc/Enter belong to the editor while typing, not to the app's shortcuts
        if (e->type() == QEvent::ShortcutOverride) {
            const int k = static_cast<QKeyEvent *>(e)->key();
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter) {
                e->accept();
                return true;
            }
        }
        if (e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Escape) {
                finishTextEdit(false);
                return true;
            }
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
                finishTextEdit(true);
                return true;
            }
        }
        if (e->type() == QEvent::FocusOut && m_textEditor->isVisible()) {
            // clicking elsewhere (but not a popup like the font list) keeps what was typed
            if (!QApplication::activePopupWidget())
                QTimer::singleShot(0, this, [this]() {
                    if (m_textEditor->isVisible() && !m_textEditor->hasFocus())
                        finishTextEdit(true);
                });
        }
    }
    if (o == m_editor) {
        if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
            e->accept();
            return true;
        }
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(e)->key() == Qt::Key_Escape) {
            m_editor->hide();
            m_commit = nullptr;
            m_canvas->setFocus();
            return true;
        }
        if (e->type() == QEvent::FocusOut && m_editor->isVisible()) {
            // clicking elsewhere saves, like most editors
            const QString t = m_editor->text();
            m_editor->hide();
            if (m_commit)
                m_commit(t);
            m_commit = nullptr;
        }
    }
    return QWidget::eventFilter(o, e);
}

void ViewerWidget::updateTime()
{
    if (!m_doc->isOpen())
        return;
    const Project &p = m_doc->project();
    m_time->setText(formatTimecode(m_doc->playhead(), p.canvas.fps) + "  /  " + formatTimecode(p.duration(), p.canvas.fps));
    m_info->setText(QString("%1×%2 · %3 fps · %4").arg(p.canvas.width).arg(p.canvas.height)
                        .arg(QString::number(p.canvas.fps.toDouble(), 'f', p.canvas.fps.den == 1 ? 0 : 2), p.canvas.aspectLabel()));
}

} // namespace ss
