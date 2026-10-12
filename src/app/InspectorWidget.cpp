#include "InspectorWidget.h"
#include "../core/TimelineOps.h"
#include "Theme.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace ss {

InspectorWidget::InspectorWidget(Document *doc, QWidget *parent) : QScrollArea(parent), m_doc(doc)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setAccessibleName("Inspector");
    connect(doc, &Document::selectionChanged, this, &InspectorWidget::rebuild);
    connect(doc, &Document::projectReset, this, &InspectorWidget::rebuild);
    connect(doc, &Document::projectChanged, this, [this]() {
        // structure may change (e.g. clip deleted) -> rebuild only when needed
        QString sig;
        for (const QString &id : m_doc->selectedClips())
            if (m_doc->project().findClip(id))
                sig += id;
        if (sig != m_signature)
            rebuild();
        else
            refreshValues();
    });
    rebuild();
}

void InspectorWidget::focusClip(const QString &clipId)
{
    m_doc->setSelectedClips({clipId});
    setFocus();
}

QGroupBox *InspectorWidget::section(const QString &title, QFormLayout **form)
{
    auto *g = new QGroupBox(title);
    auto *f = new QFormLayout(g);
    f->setLabelAlignment(Qt::AlignRight);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setRowWrapPolicy(width() < 330 ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows);
    m_layout->addWidget(g);
    *form = f;
    return g;
}

void InspectorWidget::edit(const QString &what, const QString &key, const std::function<void(Clip &, Track &)> &fn)
{
    if (m_updating)
        return;
    const QStringList ids = m_doc->selectedClips();
    m_doc->apply(what, [&](Project &p) {
        bool any = false;
        for (const QString &id : ids) {
            Track *t = nullptr;
            Clip *c = p.findClip(id, &t);
            if (!c || t->locked)
                continue;
            fn(*c, *t);
            any = true;
        }
        return any;
    }, key + ":" + ids.join(','));
}

void InspectorWidget::refreshValues()
{
    m_updating = true;
    for (auto &r : m_refreshers)
        r();
    m_updating = false;
}

void InspectorWidget::rebuild()
{
    m_refreshers.clear();
    m_body = new QWidget;
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    m_signature.clear();
    if (!m_doc->isOpen()) {
        setWidget(m_body);
        return;
    }
    const Project &p = m_doc->project();
    QStringList ids;
    bool anyVideo = false, anyAudio = false, anyTitle = false, anyShape = false, anySpeed = false;
    for (const QString &id : m_doc->selectedClips()) {
        const Track *t = nullptr;
        const Clip *c = p.findClip(id, &t);
        if (!c)
            continue;
        ids << id;
        m_signature += id;
        if (c->type == ClipType::Title)
            anyTitle = true;
        else if (c->type == ClipType::Shape)
            anyShape = true;
        else if (t->kind == TrackKind::Audio)
            anyAudio = true;
        else
            anyVideo = true;
        if (c->type == ClipType::Media) {
            const MediaAsset *m = p.findMedia(c->mediaId);
            if (m && m->kind != MediaKind::Image)
                anySpeed = true;
        }
    }

    auto *title = new QLabel;
    title->setObjectName("heading");
    m_layout->addWidget(title);
    if (ids.isEmpty()) {
        title->setText("Project");
        buildProject();
    } else {
        const Clip *first = p.findClip(ids.first());
        title->setText(ids.size() == 1 ? first->name : QString("%1 clips selected").arg(ids.size()));
        auto *timing = new QLabel;
        timing->setObjectName("hint");
        m_layout->addWidget(timing);
        m_refreshers << [this, timing, ids]() {
            const Clip *c = m_doc->project().findClip(ids.first());
            if (!c)
                return;
            timing->setText(QString("Start %1 · Duration %2 · Source %3–%4")
                                .arg(formatClock(c->start), formatClock(c->duration()), formatClock(c->in), formatClock(c->out)));
        };
        auto *enabled = new QCheckBox("Clip enabled");
        m_layout->addWidget(enabled);
        m_refreshers << [this, enabled, ids]() {
            if (const Clip *c = m_doc->project().findClip(ids.first()))
                enabled->setChecked(c->enabled);
        };
        connect(enabled, &QCheckBox::toggled, this, [this](bool on) { edit("Enable clip", "enabled", [on](Clip &c, Track &) { c.enabled = on; }); });

        const bool homogeneous = (anyVideo + anyAudio + anyTitle + anyShape) == 1;
        if (anyVideo && homogeneous)
            buildMedia(true, false, anySpeed);
        if (anyAudio && homogeneous)
            buildMedia(false, true, anySpeed);
        if (anyTitle && homogeneous)
            buildTitle();
        if (anyShape && homogeneous)
            buildShape();
        if (!homogeneous) {
            auto *l = new QLabel("Select clips of one kind to edit their properties together.");
            l->setWordWrap(true);
            l->setObjectName("hint");
            m_layout->addWidget(l);
        }
    }
    m_layout->addStretch(1);
    setWidget(m_body);
    refreshValues();
}

// ------------------------------------------------------------------ project

void InspectorWidget::buildProject()
{
    QFormLayout *f;
    section("Canvas", &f);
    auto *preset = new QComboBox;
    preset->addItem("Custom", QSize());
    preset->addItem("16:9 · 1920×1080", QSize(1920, 1080));
    preset->addItem("16:9 · 3840×2160 (4K)", QSize(3840, 2160));
    preset->addItem("16:9 · 1280×720", QSize(1280, 720));
    preset->addItem("9:16 · 1080×1920 (Shorts/Reels)", QSize(1080, 1920));
    preset->addItem("1:1 · 1080×1080", QSize(1080, 1080));
    preset->addItem("4:5 · 1080×1350", QSize(1080, 1350));
    f->addRow("Format", preset);
    auto *w = new QSpinBox;
    w->setRange(16, 8192);
    w->setSingleStep(2);
    auto *h = new QSpinBox;
    h->setRange(16, 8192);
    h->setSingleStep(2);
    auto *row = new QHBoxLayout;
    row->addWidget(w);
    row->addWidget(new QLabel("×"));
    row->addWidget(h);
    f->addRow("Size", row);
    auto *fps = new QComboBox;
    for (const auto &[label, r] : QVector<QPair<QString, Rational>>{{"23.976", {24000, 1001}}, {"24", {24, 1}}, {"25", {25, 1}},
                                                                     {"29.97", {30000, 1001}}, {"30", {30, 1}}, {"50", {50, 1}},
                                                                     {"59.94", {60000, 1001}}, {"60", {60, 1}}})
        fps->addItem(label, r.toString());
    f->addRow("Frame rate", fps);
    auto *bg = new ColorButton;
    f->addRow("Background", bg);
    auto *fillAll = new QPushButton("Fill frame with all video clips");
    fillAll->setToolTip("Useful after switching to a vertical or square format");
    f->addRow("", fillAll);

    m_refreshers << [this, w, h, fps, bg, preset]() {
        const CanvasSettings &c = m_doc->project().canvas;
        w->setValue(c.width);
        h->setValue(c.height);
        fps->setCurrentIndex(std::max(0, fps->findData(c.fps.toString())));
        bg->setColor(c.background);
        int idx = 0;
        for (int i = 1; i < preset->count(); ++i)
            if (preset->itemData(i).toSize() == QSize(c.width, c.height))
                idx = i;
        preset->setCurrentIndex(idx);
    };
    auto setCanvas = [this](const QString &what, const std::function<void(CanvasSettings &)> &fn) {
        if (m_updating)
            return;
        m_doc->apply(what, [&](Project &p) {
            fn(p.canvas);
            p.canvas.width &= ~1;
            p.canvas.height &= ~1;
            return true;
        });
    };
    connect(preset, &QComboBox::currentIndexChanged, this, [preset, setCanvas](int i) {
        const QSize s = preset->itemData(i).toSize();
        if (s.isValid())
            setCanvas("Change format", [s](CanvasSettings &c) {
                c.width = s.width();
                c.height = s.height();
            });
    });
    connect(w, &QSpinBox::editingFinished, this, [w, setCanvas]() { setCanvas("Canvas width", [w](CanvasSettings &c) { c.width = w->value(); }); });
    connect(h, &QSpinBox::editingFinished, this, [h, setCanvas]() { setCanvas("Canvas height", [h](CanvasSettings &c) { c.height = h->value(); }); });
    connect(fps, &QComboBox::currentIndexChanged, this, [fps, setCanvas](int) {
        setCanvas("Frame rate", [fps](CanvasSettings &c) { c.fps = Rational::fromString(fps->currentData().toString()); });
    });
    connect(bg, &ColorButton::colorChanged, this, [setCanvas](const QColor &col) { setCanvas("Background", [col](CanvasSettings &c) { c.background = col; }); });
    connect(fillAll, &QPushButton::clicked, this, [this]() {
        m_doc->apply("Fill frame", [](Project &p) {
            for (auto &t : p.tracks)
                if (t.kind == TrackKind::Video)
                    for (auto &c : t.clips)
                        if (c.type == ClipType::Media)
                            c.transform.fit = "fill";
            return true;
        });
    });

    auto *hint = new QLabel("Select a clip on the timeline to edit its transform, speed, colour, audio or text.");
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    m_layout->addWidget(hint);
}

// ------------------------------------------------------------------ media

void InspectorWidget::buildMedia(bool video, bool audio, bool hasSpeed)
{
    const QStringList ids = m_doc->selectedClips();
    auto first = [this, ids]() -> const Clip * { return m_doc->project().findClip(ids.first()); };
    QFormLayout *f;

    if (video) {
        section("Transform", &f);
        auto *fit = new QComboBox;
        fit->addItem("Fit (letterbox)", "fit");
        fit->addItem("Fill (crop to frame)", "fill");
        fit->addItem("Stretch", "stretch");
        f->addRow("Frame", fit);
        auto *scale = new SliderSpin(0.05, 4.0, 0.01, 2, "×");
        f->addRow("Scale", scale);
        auto *px = new SliderSpin(-1.0, 1.0, 0.005, 3);
        f->addRow("Position X", px);
        auto *py = new SliderSpin(-1.0, 1.0, 0.005, 3);
        f->addRow("Position Y", py);
        auto *rot = new SliderSpin(-180, 180, 0.5, 1, "°");
        f->addRow("Rotation", rot);
        auto *rotRow = new QHBoxLayout;
        auto *rl = new QPushButton("⟲ 90°");
        auto *rr = new QPushButton("⟳ 90°");
        auto *fh = new QCheckBox("Flip H");
        auto *fv = new QCheckBox("Flip V");
        rotRow->addWidget(rl);
        rotRow->addWidget(rr);
        rotRow->addWidget(fh);
        rotRow->addWidget(fv);
        f->addRow("", rotRow);
        auto *op = new SliderSpin(0, 1, 0.01, 2);
        f->addRow("Opacity", op);
        auto *reset = new QPushButton("Reset transform");
        f->addRow("", reset);

        QFormLayout *cf;
        section("Crop", &cf);
        auto *cl = new SliderSpin(0, 0.9, 0.005, 3);
        auto *ct = new SliderSpin(0, 0.9, 0.005, 3);
        auto *cr = new SliderSpin(0, 0.9, 0.005, 3);
        auto *cb = new SliderSpin(0, 0.9, 0.005, 3);
        cf->addRow("Left", cl);
        cf->addRow("Top", ct);
        cf->addRow("Right", cr);
        cf->addRow("Bottom", cb);

        m_refreshers << [=]() {
            const Clip *c = first();
            if (!c)
                return;
            fit->setCurrentIndex(std::max(0, fit->findData(c->transform.fit)));
            scale->setValue(c->transform.scale);
            px->setValue(c->transform.posX);
            py->setValue(c->transform.posY);
            rot->setValue(c->transform.rotation > 180 ? c->transform.rotation - 360 : c->transform.rotation);
            fh->setChecked(c->transform.flipH);
            fv->setChecked(c->transform.flipV);
            op->setValue(c->transform.opacity);
            cl->setValue(c->transform.cropL);
            ct->setValue(c->transform.cropT);
            cr->setValue(c->transform.cropR);
            cb->setValue(c->transform.cropB);
        };
        connect(fit, &QComboBox::currentIndexChanged, this, [this, fit](int) {
            const QString v = fit->currentData().toString();
            edit("Fit", "fit", [v](Clip &c, Track &) { c.transform.fit = v; });
        });
        auto bindT = [this](SliderSpin *s, const QString &key, double Transform::*member) {
            connect(s, &SliderSpin::valueChanged, this, [this, key, member](double v) {
                edit("Transform", key, [member, v](Clip &c, Track &) { c.transform.*member = v; });
            });
        };
        bindT(scale, "scale", &Transform::scale);
        bindT(px, "posX", &Transform::posX);
        bindT(py, "posY", &Transform::posY);
        bindT(op, "opacity", &Transform::opacity);
        bindT(cl, "cropL", &Transform::cropL);
        bindT(ct, "cropT", &Transform::cropT);
        bindT(cr, "cropR", &Transform::cropR);
        bindT(cb, "cropB", &Transform::cropB);
        connect(rot, &SliderSpin::valueChanged, this, [this](double v) {
            edit("Rotate", "rotation", [v](Clip &c, Track &) { c.transform.rotation = v < 0 ? v + 360 : v; });
        });
        connect(rl, &QPushButton::clicked, this, [this]() {
            edit("Rotate", "rot90", [](Clip &c, Track &) { c.transform.rotation = std::fmod(c.transform.rotation + 270, 360); });
        });
        connect(rr, &QPushButton::clicked, this, [this]() {
            edit("Rotate", "rot90", [](Clip &c, Track &) { c.transform.rotation = std::fmod(c.transform.rotation + 90, 360); });
        });
        connect(fh, &QCheckBox::toggled, this, [this](bool on) { edit("Flip", "flipH", [on](Clip &c, Track &) { c.transform.flipH = on; }); });
        connect(fv, &QCheckBox::toggled, this, [this](bool on) { edit("Flip", "flipV", [on](Clip &c, Track &) { c.transform.flipV = on; }); });
        connect(reset, &QPushButton::clicked, this, [this]() { edit("Reset transform", "reset", [](Clip &c, Track &) { c.transform = Transform(); }); });
    }

    if (hasSpeed) {
        section("Speed", &f);
        auto *speed = new QDoubleSpinBox;
        speed->setRange(0.1, 16);
        speed->setSingleStep(0.05);
        speed->setDecimals(2);
        speed->setSuffix("×");
        speed->setKeyboardTracking(false);
        f->addRow("Speed", speed);
        auto *note = new QLabel("Linked audio follows (pitch-preserving). Captions stay in sync.");
        note->setObjectName("hint");
        note->setWordWrap(true);
        f->addRow("", note);
        m_refreshers << [=]() {
            if (const Clip *c = first())
                speed->setValue(c->speed);
        };
        connect(speed, &QDoubleSpinBox::valueChanged, this, [this, ids](double v) {
            if (m_updating)
                return;
            if (!m_doc->apply("Change speed", [&](Project &p) {
                    bool ok = true;
                    for (const QString &id : ids)
                        ok = TimelineOps::setClipSpeed(p, id, v) && ok;
                    return ok;
                }))
                refreshValues();
        });
    }

    buildFades(audio);

    if (video) {
        section("Colour", &f);
        auto *ex = new SliderSpin(-3, 3, 0.05, 2, " EV");
        auto *co = new SliderSpin(0, 2, 0.01, 2);
        auto *sa = new SliderSpin(0, 2, 0.01, 2);
        auto *te = new SliderSpin(-1, 1, 0.01, 2);
        auto *ti = new SliderSpin(-1, 1, 0.01, 2);
        f->addRow("Exposure", ex);
        f->addRow("Contrast", co);
        f->addRow("Saturation", sa);
        f->addRow("Temperature", te);
        f->addRow("Tint", ti);
        auto *reset = new QPushButton("Reset colour");
        f->addRow("", reset);
        m_refreshers << [=]() {
            const Clip *c = first();
            if (!c)
                return;
            ex->setValue(c->color.exposure);
            co->setValue(c->color.contrast);
            sa->setValue(c->color.saturation);
            te->setValue(c->color.temperature);
            ti->setValue(c->color.tint);
        };
        auto bindC = [this](SliderSpin *s, const QString &key, double ColorAdjust::*member) {
            connect(s, &SliderSpin::valueChanged, this, [this, key, member](double v) {
                edit("Colour", key, [member, v](Clip &c, Track &) { c.color.*member = v; });
            });
        };
        bindC(ex, "exposure", &ColorAdjust::exposure);
        bindC(co, "contrast", &ColorAdjust::contrast);
        bindC(sa, "saturation", &ColorAdjust::saturation);
        bindC(te, "temperature", &ColorAdjust::temperature);
        bindC(ti, "tint", &ColorAdjust::tint);
        connect(reset, &QPushButton::clicked, this, [this]() { edit("Reset colour", "colorreset", [](Clip &c, Track &) { c.color = ColorAdjust(); }); });
    }

    if (audio) {
        section("Audio", &f);
        auto *gain = new SliderSpin(-40, 20, 0.5, 1, " dB");
        f->addRow("Gain", gain);
        auto *mute = new QCheckBox("Mute clip");
        f->addRow("", mute);
        auto *tv = new SliderSpin(-40, 12, 0.5, 1, " dB");
        f->addRow("Track volume", tv);
        m_refreshers << [=]() {
            const Track *t = nullptr;
            const Clip *c = m_doc->project().findClip(ids.first(), &t);
            if (!c)
                return;
            gain->setValue(c->audio.gainDb);
            mute->setChecked(c->audio.mute);
            tv->setValue(t->volumeDb);
        };
        connect(gain, &SliderSpin::valueChanged, this, [this](double v) { edit("Gain", "gain", [v](Clip &c, Track &) { c.audio.gainDb = v; }); });
        connect(mute, &QCheckBox::toggled, this, [this](bool on) { edit("Mute", "mute", [on](Clip &c, Track &) { c.audio.mute = on; }); });
        connect(tv, &SliderSpin::valueChanged, this, [this](double v) { edit("Track volume", "trackvol", [v](Clip &, Track &t) { t.volumeDb = v; }); });
    }
}

void InspectorWidget::buildFades(bool audio)
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    section(audio ? "Audio fades" : "Transitions", &f);
    auto *fi = new QDoubleSpinBox;
    auto *fo = new QDoubleSpinBox;
    for (auto *s : {fi, fo}) {
        s->setRange(0, 30);
        s->setSingleStep(0.1);
        s->setDecimals(2);
        s->setSuffix(" s");
        s->setKeyboardTracking(false);
    }
    f->addRow(audio ? "Fade in" : "Fade in (from transparent)", fi);
    f->addRow(audio ? "Fade out" : "Fade out", fo);
    if (!audio) {
        auto *hint = new QLabel("Overlap clips on two video tracks and fade the upper one for a cross-dissolve; "
                                "fade over the background for a dip to black.");
        hint->setWordWrap(true);
        hint->setObjectName("hint");
        f->addRow("", hint);
    }
    m_refreshers << [=]() {
        const Clip *c = m_doc->project().findClip(ids.first());
        if (!c)
            return;
        fi->setValue((audio ? c->audio.fadeIn : c->fadeIn) / 1e6);
        fo->setValue((audio ? c->audio.fadeOut : c->fadeOut) / 1e6);
    };
    connect(fi, &QDoubleSpinBox::valueChanged, this, [this, audio](double v) {
        edit("Fade in", "fadein", [v, audio](Clip &c, Track &) {
            const TimeUs t = std::min<TimeUs>(TimeUs(v * kUsPerSec), c.duration());
            (audio ? c.audio.fadeIn : c.fadeIn) = t;
        });
    });
    connect(fo, &QDoubleSpinBox::valueChanged, this, [this, audio](double v) {
        edit("Fade out", "fadeout", [v, audio](Clip &c, Track &) {
            const TimeUs t = std::min<TimeUs>(TimeUs(v * kUsPerSec), c.duration());
            (audio ? c.audio.fadeOut : c.fadeOut) = t;
        });
    });
}

// ------------------------------------------------------------------ title

void InspectorWidget::buildTitle()
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    section("Text", &f);
    auto *text = new QPlainTextEdit;
    text->setMaximumHeight(80);
    f->addRow("Text", text);
    auto *font = new QFontComboBox;
    f->addRow("Font", font);
    auto *weight = new QComboBox;
    for (int w : {300, 400, 500, 600, 700, 800, 900})
        weight->addItem(QString::number(w), w);
    auto *italic = new QCheckBox("Italic");
    auto *wr = new QHBoxLayout;
    wr->addWidget(weight);
    wr->addWidget(italic);
    f->addRow("Weight", wr);
    auto *size = new SliderSpin(0.01, 0.3, 0.002, 3);
    f->addRow("Size", size);
    auto *color = new ColorButton;
    auto *outline = new ColorButton;
    auto *bg = new ColorButton;
    auto *cr = new QHBoxLayout;
    cr->addWidget(new QLabel("Text"));
    cr->addWidget(color);
    cr->addWidget(new QLabel("Outline"));
    cr->addWidget(outline);
    cr->addWidget(new QLabel("Box"));
    cr->addWidget(bg);
    cr->addStretch();
    f->addRow("Colours", cr);
    auto *ow = new SliderSpin(0, 0.3, 0.005, 3);
    f->addRow("Outline width", ow);
    auto *x = new SliderSpin(0, 1, 0.005, 3);
    auto *y = new SliderSpin(0, 1, 0.005, 3);
    f->addRow("Position X", x);
    f->addRow("Position Y", y);
    auto *align = new QComboBox;
    align->addItem("Left", "left");
    align->addItem("Center", "center");
    align->addItem("Right", "right");
    f->addRow("Align", align);
    auto *anim = new QComboBox;
    for (const auto &[l, v] : QVector<QPair<QString, QString>>{{"None", "none"}, {"Fade", "fade"}, {"Slide up", "slideUp"}, {"Pop", "pop"}, {"Typewriter", "typewriter"}})
        anim->addItem(l, v);
    f->addRow("Animation", anim);
    auto *ad = new QDoubleSpinBox;
    ad->setRange(0, 5);
    ad->setSingleStep(0.1);
    ad->setSuffix(" s");
    ad->setKeyboardTracking(false);
    f->addRow("Anim. duration", ad);
    auto *presets = new QHBoxLayout;
    auto *lower = new QPushButton("Lower third");
    auto *center = new QPushButton("Centre");
    auto *topl = new QPushButton("Top label");
    presets->addWidget(lower);
    presets->addWidget(center);
    presets->addWidget(topl);
    f->addRow("Layout", presets);

    m_refreshers << [=]() {
        const Clip *c = m_doc->project().findClip(ids.first());
        if (!c)
            return;
        const TitleProps &t = c->title;
        if (text->toPlainText() != t.text)
            text->setPlainText(t.text);
        font->setCurrentFont(QFont(t.fontFamily));
        weight->setCurrentIndex(std::max(0, weight->findData(t.weight)));
        italic->setChecked(t.italic);
        size->setValue(t.sizePct);
        color->setColor(t.color);
        outline->setColor(t.outlineColor);
        bg->setColor(t.backgroundColor);
        ow->setValue(t.outlineWidthPct);
        x->setValue(t.x);
        y->setValue(t.y);
        align->setCurrentIndex(std::max(0, align->findData(t.align)));
        anim->setCurrentIndex(std::max(0, anim->findData(t.animation)));
        ad->setValue(t.animationDuration / 1e6);
    };
    auto E = [this](const QString &key, const std::function<void(TitleProps &)> &fn) {
        edit("Edit title", key, [fn](Clip &c, Track &) {
            fn(c.title);
            c.name = c.title.text.simplified().left(24);
        });
    };
    connect(text, &QPlainTextEdit::textChanged, this, [text, E]() { const QString s = text->toPlainText(); E("text", [s](TitleProps &t) { t.text = s; }); });
    connect(font, &QFontComboBox::currentFontChanged, this, [E](const QFont &fnt) { const QString fam = fnt.family(); E("font", [fam](TitleProps &t) { t.fontFamily = fam; }); });
    connect(weight, &QComboBox::currentIndexChanged, this, [weight, E](int) { const int w = weight->currentData().toInt(); E("weight", [w](TitleProps &t) { t.weight = w; }); });
    connect(italic, &QCheckBox::toggled, this, [E](bool on) { E("italic", [on](TitleProps &t) { t.italic = on; }); });
    connect(size, &SliderSpin::valueChanged, this, [E](double v) { E("size", [v](TitleProps &t) { t.sizePct = v; }); });
    connect(color, &ColorButton::colorChanged, this, [E](const QColor &c) { E("color", [c](TitleProps &t) { t.color = c; }); });
    connect(outline, &ColorButton::colorChanged, this, [E](const QColor &c) { E("outline", [c](TitleProps &t) { t.outlineColor = c; }); });
    connect(bg, &ColorButton::colorChanged, this, [E](const QColor &c) { E("bg", [c](TitleProps &t) { t.backgroundColor = c; }); });
    connect(ow, &SliderSpin::valueChanged, this, [E](double v) { E("ow", [v](TitleProps &t) { t.outlineWidthPct = v; }); });
    connect(x, &SliderSpin::valueChanged, this, [E](double v) { E("x", [v](TitleProps &t) { t.x = v; }); });
    connect(y, &SliderSpin::valueChanged, this, [E](double v) { E("y", [v](TitleProps &t) { t.y = v; }); });
    connect(align, &QComboBox::currentIndexChanged, this, [align, E](int) { const QString a = align->currentData().toString(); E("align", [a](TitleProps &t) { t.align = a; }); });
    connect(anim, &QComboBox::currentIndexChanged, this, [anim, E](int) { const QString a = anim->currentData().toString(); E("anim", [a](TitleProps &t) { t.animation = a; }); });
    connect(ad, &QDoubleSpinBox::valueChanged, this, [E](double v) { E("animdur", [v](TitleProps &t) { t.animationDuration = TimeUs(v * kUsPerSec); }); });
    connect(lower, &QPushButton::clicked, this, [E]() {
        E("layout", [](TitleProps &t) {
            t.x = 0.3;
            t.y = 0.82;
            t.align = "left";
            t.sizePct = 0.05;
            t.backgroundColor = QColor(0, 0, 0, 170);
            t.animation = "slideUp";
        });
    });
    connect(center, &QPushButton::clicked, this, [E]() {
        E("layout", [](TitleProps &t) {
            t.x = 0.5;
            t.y = 0.5;
            t.align = "center";
            t.sizePct = 0.1;
            t.backgroundColor = QColor(0, 0, 0, 0);
            t.animation = "pop";
        });
    });
    connect(topl, &QPushButton::clicked, this, [E]() {
        E("layout", [](TitleProps &t) {
            t.x = 0.5;
            t.y = 0.12;
            t.align = "center";
            t.sizePct = 0.045;
            t.backgroundColor = QColor(91, 140, 255, 220);
            t.animation = "fade";
        });
    });
    buildFades(false);
}

// ------------------------------------------------------------------ shape

void InspectorWidget::buildShape()
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    section("Shape", &f);
    auto *kind = new QComboBox;
    kind->addItem("Rectangle", "rect");
    kind->addItem("Ellipse", "ellipse");
    kind->addItem("Line", "line");
    kind->addItem("Arrow", "arrow");
    kind->addItem("Triangle", "triangle");
    kind->addItem("Star", "star");
    f->addRow("Shape", kind);
    auto *fill = new ColorButton;
    auto *stroke = new ColorButton;
    auto *cr = new QHBoxLayout;
    cr->addWidget(new QLabel("Fill"));
    cr->addWidget(fill);
    cr->addWidget(new QLabel("Stroke"));
    cr->addWidget(stroke);
    cr->addStretch();
    f->addRow("Colours", cr);
    auto *sw = new SliderSpin(0, 0.05, 0.001, 3);
    f->addRow("Stroke width", sw);
    auto *x = new SliderSpin(0, 1, 0.005, 3);
    auto *y = new SliderSpin(0, 1, 0.005, 3);
    auto *w = new SliderSpin(0.005, 1.5, 0.005, 3);
    auto *h = new SliderSpin(0.002, 1.5, 0.005, 3);
    auto *r = new SliderSpin(0, 0.5, 0.01, 2);
    f->addRow("Centre X", x);
    f->addRow("Centre Y", y);
    f->addRow("Width", w);
    f->addRow("Height", h);
    f->addRow("Corner radius", r);
    m_refreshers << [=]() {
        const Clip *c = m_doc->project().findClip(ids.first());
        if (!c)
            return;
        const ShapeProps &s = c->shape;
        kind->setCurrentIndex(std::max(0, kind->findData(s.shape)));
        fill->setColor(s.fill);
        stroke->setColor(s.stroke);
        sw->setValue(s.strokeWidthPct);
        x->setValue(s.x);
        y->setValue(s.y);
        w->setValue(s.w);
        h->setValue(s.h);
        r->setValue(s.radiusPct);
    };
    auto E = [this](const QString &key, const std::function<void(ShapeProps &)> &fn) {
        edit("Edit shape", key, [fn](Clip &c, Track &) { fn(c.shape); });
    };
    connect(kind, &QComboBox::currentIndexChanged, this, [kind, E](int) { const QString k = kind->currentData().toString(); E("kind", [k](ShapeProps &s) { s.shape = k; }); });
    connect(fill, &ColorButton::colorChanged, this, [E](const QColor &c) { E("fill", [c](ShapeProps &s) { s.fill = c; }); });
    connect(stroke, &ColorButton::colorChanged, this, [E](const QColor &c) { E("stroke", [c](ShapeProps &s) { s.stroke = c; }); });
    connect(sw, &SliderSpin::valueChanged, this, [E](double v) { E("sw", [v](ShapeProps &s) { s.strokeWidthPct = v; }); });
    connect(x, &SliderSpin::valueChanged, this, [E](double v) { E("x", [v](ShapeProps &s) { s.x = v; }); });
    connect(y, &SliderSpin::valueChanged, this, [E](double v) { E("y", [v](ShapeProps &s) { s.y = v; }); });
    connect(w, &SliderSpin::valueChanged, this, [E](double v) { E("w", [v](ShapeProps &s) { s.w = v; }); });
    connect(h, &SliderSpin::valueChanged, this, [E](double v) { E("h", [v](ShapeProps &s) { s.h = v; }); });
    connect(r, &SliderSpin::valueChanged, this, [E](double v) { E("r", [v](ShapeProps &s) { s.radiusPct = v; }); });
    buildFades(false);
}

} // namespace ss
