#include "EffectsBrowser.h"
#include "../core/TimelineOps.h"

#include <QHeaderView>
#include <QMimeData>
#include <algorithm>

namespace ss {

EffectsBrowser::EffectsBrowser(Document *doc, QWidget *parent) : QTreeWidget(parent), m_doc(doc)
{
    setHeaderHidden(true);
    setAccessibleName("Effects browser");
    setRootIsDecorated(true);
    const QVector<QPair<QString, QVector<QPair<QString, QString>>>> groups{
        {"Transitions",
         {{"Fade in (0.5 s)", "fade-in"}, {"Fade out (0.5 s)", "fade-out"}, {"Fade in + out (1 s)", "fade-both"},
          {"Dip to black (between clips)", "dip-black"}, {"Remove fades", "fade-none"}}},
        {"Titles",
         {{"Centre title", "title-center"}, {"Lower third", "title-lower"}, {"Top label", "title-top"},
          {"Call to action", "title-cta"}}},
        {"Shapes",
         {{"Arrow", "shape-arrow"}, {"Highlight box", "shape-highlight"}, {"Circle", "shape-circle"}, {"Star burst", "shape-star"},
          {"Pill tag", "shape-pill"}, {"Triangle", "shape-triangle"}, {"Lower bar", "shape-bar"}, {"Divider line", "shape-line"},
          {"Frame vignette box", "shape-box"}}},
        {"Colour looks",
         {{"Warm", "look-warm"}, {"Cool", "look-cool"}, {"Vivid", "look-vivid"}, {"Black & white", "look-bw"},
          {"Faded film", "look-faded"}, {"High contrast", "look-contrast"}, {"Brighten (+0.5 EV)", "look-bright"},
          {"Reset colour", "look-reset"}}},
        {"Framing", {{"Fill frame", "frame-fill"}, {"Fit frame", "frame-fit"}, {"Punch-in 120%", "frame-punch"}, {"Rotate 90°", "frame-rotate"}}},
        {"Speed", {{"0.5× slow motion", "speed-0.5"}, {"1× normal", "speed-1"}, {"1.5×", "speed-1.5"}, {"2×", "speed-2"}}},
        {"Audio", {{"Fade audio in/out (1 s)", "audio-fade"}, {"−6 dB", "audio-minus6"}, {"Duck music to −14 dB", "audio-duck"}, {"Mute", "audio-mute"}}},
    };
    for (const auto &g : groups) {
        auto *top = new QTreeWidgetItem(this, {g.first});
        top->setFlags(Qt::ItemIsEnabled);
        for (const auto &e : g.second) {
            auto *it = new QTreeWidgetItem(top, {e.first});
            it->setData(0, Qt::UserRole, e.second);
            const bool placeable = e.second.startsWith("title-") || e.second.startsWith("shape-");
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (placeable ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
            it->setToolTip(0, placeable ? "Double-click to add at the playhead, or drag it onto the video to place it"
                                        : "Double-click to apply to the selected clips");
        }
        top->setExpanded(true);
    }
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!id.isEmpty())
            applyEffect(id);
    });
}

QStringList EffectsBrowser::mimeTypes() const
{
    return {"application/x-shyftr-effect"};
}

QMimeData *EffectsBrowser::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    if (items.isEmpty())
        return nullptr;
    const QString id = items.first()->data(0, Qt::UserRole).toString();
    if (id.isEmpty())
        return nullptr;
    auto *md = new QMimeData;
    md->setData("application/x-shyftr-effect", id.toUtf8());
    md->setText(items.first()->text(0));
    return md;
}

void EffectsBrowser::applyEffect(const QString &id, const QPointF &pos)
{
    const bool placed = pos.x() >= 0 && pos.y() >= 0;
    if (!m_doc->isOpen())
        return;
    const QStringList sel = m_doc->selectedClips();
    const TimeUs at = m_doc->playhead();
    auto forSel = [&](const QString &what, const std::function<void(Clip &, Track &)> &fn) {
        if (sel.isEmpty()) {
            emit statusMessage("Select one or more clips first");
            return;
        }
        m_doc->apply(what, [&](Project &p) {
            for (const QString &cid : sel) {
                Track *t = nullptr;
                Clip *c = p.findClip(cid, &t);
                if (c && !t->locked)
                    fn(*c, *t);
            }
            return true;
        });
    };
    auto look = [&](double ex, double co, double sa, double te, double ti) {
        forSel("Colour look", [=](Clip &c, Track &) {
            c.color.exposure = ex;
            c.color.contrast = co;
            c.color.saturation = sa;
            c.color.temperature = te;
            c.color.tint = ti;
        });
    };
    const TimeUs half = 500 * kUsPerMs;
    if (id == "fade-in")
        forSel("Fade in", [half](Clip &c, Track &t) { (t.kind == TrackKind::Audio ? c.audio.fadeIn : c.fadeIn) = std::min(half, c.duration()); });
    else if (id == "fade-out")
        forSel("Fade out", [half](Clip &c, Track &t) { (t.kind == TrackKind::Audio ? c.audio.fadeOut : c.fadeOut) = std::min(half, c.duration()); });
    else if (id == "fade-both")
        forSel("Fade in + out", [](Clip &c, Track &t) {
            const TimeUs f = std::min<TimeUs>(kUsPerSec, c.duration() / 2);
            if (t.kind == TrackKind::Audio)
                c.audio.fadeIn = c.audio.fadeOut = f;
            else
                c.fadeIn = c.fadeOut = f;
        });
    else if (id == "dip-black")
        forSel("Dip to black", [half](Clip &c, Track &t) {
            if (t.kind == TrackKind::Audio)
                c.audio.fadeIn = c.audio.fadeOut = std::min(half, c.duration() / 2);
            else
                c.fadeIn = c.fadeOut = std::min(half, c.duration() / 2);
        });
    else if (id == "fade-none")
        forSel("Remove fades", [](Clip &c, Track &) { c.fadeIn = c.fadeOut = c.audio.fadeIn = c.audio.fadeOut = 0; });
    else if (id.startsWith("title-")) {
        TitleProps tp;
        if (id == "title-center") {
            tp.text = "Your title";
            tp.sizePct = 0.1;
            tp.animation = "pop";
        } else if (id == "title-lower") {
            tp.text = "Name Surname\nRole · Company";
            tp.sizePct = 0.045;
            tp.x = 0.28;
            tp.y = 0.8;
            tp.align = "left";
            tp.backgroundColor = QColor(0, 0, 0, 170);
            tp.animation = "slideUp";
        } else if (id == "title-top") {
            tp.text = "CHAPTER 1";
            tp.sizePct = 0.04;
            tp.y = 0.1;
            tp.backgroundColor = QColor(91, 140, 255, 220);
            tp.animation = "fade";
        } else {
            tp.text = "Follow for more →";
            tp.sizePct = 0.06;
            tp.y = 0.72;
            tp.color = QColor("#111111");
            tp.backgroundColor = QColor("#FFD43B");
            tp.animation = "pop";
        }
        if (placed) {
            tp.x = tp.align == "left" ? std::clamp(pos.x() + 0.12, 0.0, 1.0) : pos.x(); // lower thirds grow from the drop point
            tp.y = pos.y();
        }
        m_doc->apply("Add title", [&](Project &p) { return !TimelineOps::addTitleClip(p, {}, at, 4 * kUsPerSec, tp).isEmpty(); });
    } else if (id.startsWith("shape-")) {
        ShapeProps sp;
        if (id == "shape-bar") {
            sp.fill = QColor(0, 0, 0, 160);
            sp.y = 0.86;
            sp.w = 1.0;
            sp.h = 0.16;
        } else if (id == "shape-circle") {
            sp.shape = "ellipse";
            sp.fill = QColor(255, 212, 59, 230);
            sp.w = 0.12;
            sp.h = 0.21;
        } else if (id == "shape-line") {
            sp.shape = "line";
            sp.fill = Qt::white;
            sp.w = 0.4;
            sp.h = 0.006;
            sp.y = 0.62;
        } else if (id == "shape-arrow") {
            sp.shape = "arrow";
            sp.fill = QColor("#FFD43B");
            sp.w = 0.16;
            sp.h = 0.09;
        } else if (id == "shape-highlight") {
            sp.fill = QColor(0, 0, 0, 0);
            sp.stroke = QColor("#FF3B5C");
            sp.strokeWidthPct = 0.008;
            sp.w = 0.22;
            sp.h = 0.18;
            sp.radiusPct = 0.12;
        } else if (id == "shape-star") {
            sp.shape = "star";
            sp.fill = QColor("#FFD43B");
            sp.w = 0.1;
            sp.h = 0.1 * 16.0 / 9.0;
        } else if (id == "shape-pill") {
            sp.fill = QColor("#5B8CFF");
            sp.w = 0.2;
            sp.h = 0.07;
            sp.radiusPct = 0.5;
        } else if (id == "shape-triangle") {
            sp.shape = "triangle";
            sp.fill = QColor(255, 255, 255, 230);
            sp.w = 0.08;
            sp.h = 0.12;
        } else {
            sp.fill = QColor(0, 0, 0, 0);
            sp.stroke = QColor(255, 255, 255, 200);
            sp.strokeWidthPct = 0.008;
            sp.w = 0.9;
            sp.h = 0.85;
            sp.radiusPct = 0.04;
        }
        if (m_doc->isOpen() && (id == "shape-circle" || id == "shape-star")) { // round on any aspect ratio
            const auto &cv = m_doc->project().canvas;
            sp.h = sp.w * cv.width / std::max(1, cv.height);
        }
        if (placed) {
            if (sp.w < 1.0)
                sp.x = pos.x();
            sp.y = pos.y();
        }
        m_doc->apply("Add shape", [&](Project &p) { return !TimelineOps::addShapeClip(p, {}, at, 4 * kUsPerSec, sp).isEmpty(); });
    } else if (id == "look-warm")
        look(0.05, 1.05, 1.1, 0.45, 0.05);
    else if (id == "look-cool")
        look(0, 1.05, 0.95, -0.45, 0);
    else if (id == "look-vivid")
        look(0.05, 1.15, 1.45, 0.05, 0);
    else if (id == "look-bw")
        look(0, 1.15, 0, 0, 0);
    else if (id == "look-faded")
        look(0.15, 0.8, 0.7, 0.15, 0);
    else if (id == "look-contrast")
        look(0, 1.35, 1.1, 0, 0);
    else if (id == "look-bright")
        look(0.5, 1, 1, 0, 0);
    else if (id == "look-reset")
        look(0, 1, 1, 0, 0);
    else if (id == "frame-fill")
        forSel("Fill frame", [](Clip &c, Track &) { c.transform.fit = "fill"; });
    else if (id == "frame-fit")
        forSel("Fit frame", [](Clip &c, Track &) { c.transform.fit = "fit"; });
    else if (id == "frame-punch")
        forSel("Punch-in", [](Clip &c, Track &) { c.transform.scale = 1.2; });
    else if (id == "frame-rotate")
        forSel("Rotate", [](Clip &c, Track &) { c.transform.rotation = std::fmod(c.transform.rotation + 90, 360); });
    else if (id.startsWith("speed-")) {
        const double s = id.mid(6).toDouble();
        if (sel.isEmpty())
            return emit statusMessage("Select a clip first");
        if (!m_doc->apply("Change speed", [&](Project &p) {
                bool ok = true;
                for (const QString &cid : sel)
                    ok = TimelineOps::setClipSpeed(p, cid, s) && ok;
                return ok;
            }))
            emit statusMessage("Not enough room after the clip for that speed");
    } else if (id == "audio-fade")
        forSel("Audio fades", [](Clip &c, Track &) { c.audio.fadeIn = c.audio.fadeOut = std::min<TimeUs>(kUsPerSec, c.duration() / 2); });
    else if (id == "audio-minus6")
        forSel("Gain −6 dB", [](Clip &c, Track &) { c.audio.gainDb = -6; });
    else if (id == "audio-duck")
        forSel("Duck", [](Clip &c, Track &) { c.audio.gainDb = -14; });
    else if (id == "audio-mute")
        forSel("Mute", [](Clip &c, Track &) { c.audio.mute = !c.audio.mute; });
}

} // namespace ss
