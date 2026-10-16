#include "InspectorWidget.h"
#include "../captions/OverlayRenderer.h"
#include "../core/TimelineOps.h"
#include "../media/ImageFx.h"
#include "Theme.h"
#include "Widgets.h"
#include "TextStyleUi.h"
#include "../captions/TextStyles.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace ss {

namespace {
QString prettyName(const QString &id)
{
    static const QHash<QString, QString> names{
        {"none", "None"}, {"fade", "Fade"}, {"slideUp", "Slide up"}, {"slideDown", "Slide down"}, {"slideLeft", "Slide left"},
        {"slideRight", "Slide right"}, {"pop", "Pop"}, {"zoom", "Zoom"}, {"typewriter", "Typewriter"}, {"blur", "Blur"},
        {"bounce", "Bounce"}, {"spin", "Spin"}, {"drop", "Drop"}, {"auto", "Automatic"}, {"", "None"}, {"pulse", "Pulse"},
        {"shake", "Shake"}, {"float", "Float"}, {"wiggle", "Wiggle"}, {"blink", "Blink"}, {"glow", "Glow"},
        {"smooth", "Smooth"}, {"linear", "Linear"}, {"easeIn", "Ease in"}, {"easeOut", "Ease out"}, {"hold", "Hold"},
        {"montage", "Montage"}, {"hero", "Hero"}, {"bullet", "Bullet time"}, {"jumpCut", "Jump cut"}, {"flashIn", "Flash in"},
        {"flashOut", "Flash out"}, {"rampUp", "Ramp up"}, {"rampDown", "Ramp down"}, {"chipmunk", "Chipmunk"},
        {"deep", "Deep voice"}, {"robot", "Robot"}, {"echo", "Echo"}, {"hall", "Hall reverb"}, {"telephone", "Telephone"},
        {"megaphone", "Megaphone"}, {"radio", "Radio"}, {"underwater", "Underwater"}, {"monster", "Monster"},
        {"normal", "Normal"}, {"multiply", "Multiply"}, {"screen", "Screen"}, {"overlay", "Overlay"}, {"darken", "Darken"},
        {"lighten", "Lighten"}, {"color-dodge", "Colour dodge"}, {"color-burn", "Colour burn"}, {"hard-light", "Hard light"},
        {"soft-light", "Soft light"}, {"difference", "Difference"}, {"exclusion", "Exclusion"}, {"add", "Add"}};
    return names.value(id, id);
}

QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}
} // namespace

InspectorWidget::InspectorWidget(Document *doc, QWidget *parent) : QScrollArea(parent), m_doc(doc)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // the content always fits the panel width
    setAccessibleName("Inspector");
    connect(doc, &Document::selectionChanged, this, &InspectorWidget::rebuild);
    connect(doc, &Document::projectReset, this, &InspectorWidget::rebuild);
    connect(doc, &Document::projectChanged, this, [this]() {
        // structure may change (e.g. clip deleted) -> rebuild only when needed
        QString sig;
        bool keyed = false;
        for (const QString &id : m_doc->selectedClips())
            if (const Clip *c = m_doc->project().findClip(id)) {
                sig += id + QString::number(int(c->type));
                keyed = keyed || !c->fx.keyframes.isEmpty();
            }
        m_keyframed = keyed;
        if (sig != m_signature)
            rebuild();
        else
            refreshValues();
    });
    connect(doc, &Document::playheadChanged, this, [this]() {
        // animated values follow the playhead
        if (m_keyframed)
            refreshValues();
    });
    rebuild();
}

void InspectorWidget::focusClip(const QString &clipId)
{
    m_doc->setSelectedClips({clipId});
    setFocus();
}

void InspectorWidget::showTab(const QString &name)
{
    if (!m_tabs)
        return;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i) == name)
            m_tabs->setCurrentIndex(i);
}

const Clip *InspectorWidget::firstClip() const
{
    const QStringList ids = m_doc->selectedClips();
    return ids.isEmpty() ? nullptr : m_doc->project().findClip(ids.first());
}

QWidget *InspectorWidget::page(const QString &title)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 6, 0, 0);
    v->setSpacing(8);
    m_tabs->addTab(w, title);
    m_layout = v;
    return w;
}

QGroupBox *InspectorWidget::section(const QString &title, QFormLayout **form)
{
    auto *g = new QGroupBox(title);
    auto *f = new QFormLayout(g);
    f->setLabelAlignment(Qt::AlignRight);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setRowWrapPolicy(m_wrapped ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows);
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

void InspectorWidget::editAnimated(const QString &what, const QString &key, const std::function<void(AnimatedValues &)> &fn)
{
    const TimeUs ph = m_doc->playhead();
    edit(what, key, [fn, ph](Clip &c, Track &) {
        if (c.fx.keyframes.isEmpty()) {
            AnimatedValues v = c.baseValues();
            fn(v);
            c.setBaseValues(v);
            return;
        }
        const TimeUs local = std::clamp<TimeUs>(ph - c.start, 0, c.duration());
        int idx = Keyframes::indexAt(c.fx.keyframes, local, 20 * kUsPerMs);
        if (idx < 0) {
            const AnimatedValues v = Keyframes::evaluate(c.fx.keyframes, local);
            c.fx.keyframes << Keyframe{local, v.x, v.y, v.scale, v.rotation, v.opacity, "smooth"};
            Keyframes::sort(c.fx.keyframes);
            idx = Keyframes::indexAt(c.fx.keyframes, local, 0);
        }
        Keyframe &k = c.fx.keyframes[idx];
        AnimatedValues v{k.x, k.y, k.scale, k.rotation, k.opacity};
        fn(v);
        k.x = v.x;
        k.y = v.y;
        k.scale = v.scale;
        k.rotation = v.rotation;
        k.opacity = std::clamp(v.opacity, 0.0, 1.0);
    });
}

void InspectorWidget::refreshValues()
{
    m_updating = true;
    for (auto &r : m_refreshers)
        r();
    m_updating = false;
}

// Let every control shrink to the panel width instead of pushing the content off the right
// edge (spin boxes and long button labels otherwise demand their full natural width).
void InspectorWidget::fitNarrow(QWidget *root)
{
    for (QAbstractSpinBox *sb : root->findChildren<QAbstractSpinBox *>()) {
        sb->setMinimumWidth(52);
        sb->setSizePolicy(QSizePolicy::Expanding, sb->sizePolicy().verticalPolicy());
    }
    for (QComboBox *cb : root->findChildren<QComboBox *>()) {
        cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cb->setMinimumContentsLength(4);
        cb->setMinimumWidth(60);
    }
    for (QPushButton *b : root->findChildren<QPushButton *>()) {
        if (b->toolTip().isEmpty() && b->text().size() >= 14)
            b->setToolTip(b->text());
        b->setMinimumWidth(std::min(b->minimumSizeHint().width(), 40));
    }
    for (QLineEdit *le : root->findChildren<QLineEdit *>())
        le->setMinimumWidth(40);
}

void InspectorWidget::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    const bool wrap = viewport()->width() < 340;
    if (wrap != m_wrapped) {
        m_wrapped = wrap;
        rebuild();
    }
}

void InspectorWidget::rebuild()
{
    m_wrapped = viewport()->width() < 340;
    if (m_tabs && m_tabs->currentIndex() >= 0)
        m_lastTab = m_tabs->tabText(m_tabs->currentIndex());
    m_refreshers.clear();
    m_tabs = nullptr;
    m_body = new QWidget;
    auto *root = new QVBoxLayout(m_body);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    m_layout = root;
    m_signature.clear();
    m_keyframed = false;
    if (!m_doc->isOpen()) {
        fitNarrow(m_body);
        setWidget(m_body);
        return;
    }
    const Project &p = m_doc->project();
    QStringList ids;
    bool anyVideo = false, anyAudio = false, anyTitle = false, anyShape = false, anyEffect = false, anySpeed = false;
    for (const QString &id : m_doc->selectedClips()) {
        const Track *t = nullptr;
        const Clip *c = p.findClip(id, &t);
        if (!c)
            continue;
        ids << id;
        m_signature += id + QString::number(int(c->type));
        m_keyframed = m_keyframed || !c->fx.keyframes.isEmpty();
        if (c->type == ClipType::Title)
            anyTitle = true;
        else if (c->type == ClipType::Shape)
            anyShape = true;
        else if (c->type == ClipType::Effect)
            anyEffect = true;
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
    title->setWordWrap(true);
    root->addWidget(title);
    if (ids.isEmpty()) {
        title->setText("Project");
        buildProject();
        root->addStretch(1);
        fitNarrow(m_body);
        setWidget(m_body);
        refreshValues();
        return;
    }
    const Clip *first = p.findClip(ids.first());
    title->setText(ids.size() == 1 ? first->name : QString("%1 clips selected").arg(ids.size()));
    auto *timing = new QLabel;
    timing->setObjectName("hint");
    timing->setWordWrap(true);
    root->addWidget(timing);
    m_refreshers << [this, timing, ids]() {
        const Clip *c = m_doc->project().findClip(ids.first());
        if (!c)
            return;
        timing->setText(QString("Start %1 · Duration %2").arg(formatClock(c->start), formatClock(c->duration())));
    };
    auto *enabled = new QCheckBox("Clip enabled");
    root->addWidget(enabled);
    m_refreshers << [this, enabled, ids]() {
        if (const Clip *c = m_doc->project().findClip(ids.first()))
            enabled->setChecked(c->enabled);
    };
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) { edit("Enable clip", "enabled", [on](Clip &c, Track &) { c.enabled = on; }); });

    const bool homogeneous = (anyVideo + anyAudio + anyTitle + anyShape + anyEffect) == 1;
    if (!homogeneous) {
        root->addWidget(hint("Select clips of one kind to edit their properties together."));
        root->addStretch(1);
        fitNarrow(m_body);
        setWidget(m_body);
        refreshValues();
        return;
    }
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    root->addWidget(m_tabs, 1);

    if (anyVideo) {
        page("Basic");
        buildTransform(true);
        buildTransition();
        buildFades(false);
        buildCrop();
        buildColour();
        page("Animate");
        buildKeyframes();
        page("Effects");
        buildEffects(true);
        page("Cutout");
        buildCutout();
        page("Speed");
        buildSpeed(anySpeed, true);
    } else if (anyAudio) {
        page("Audio");
        buildAudio();
        buildFades(true);
        page("Voice");
        buildVoice();
        if (anySpeed) {
            page("Speed");
            buildSpeed(true, false);
        }
    } else if (anyTitle) {
        page("Text");
        buildTitleText();
        buildFades(false);
        page("Style");
        buildTitleStyle();
        page("Animate");
        buildTitleAnimation();
        buildTransform(false);
        buildKeyframes();
        page("Effects");
        buildEffects(false);
    } else if (anyShape) {
        page("Shape");
        buildShape();
        buildFades(false);
        page("Animate");
        buildTransform(false);
        buildKeyframes();
        page("Effects");
        buildEffects(false);
    } else if (anyEffect) {
        page("Effects");
        buildEffects(true);
        buildFades(false);
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *v = qobject_cast<QVBoxLayout *>(m_tabs->widget(i)->layout()))
            v->addStretch(1);
    if (!m_lastTab.isEmpty())
        showTab(m_lastTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) { m_lastTab = m_tabs->tabText(i); });
    // narrow panels: combo boxes and the tab bar must not force a wider layout
    for (QComboBox *c : m_body->findChildren<QComboBox *>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(6);
        c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    m_tabs->setMinimumWidth(0);
    m_body->setMinimumWidth(0);
    fitNarrow(m_body);
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

// ------------------------------------------------------------------ transform

void InspectorWidget::buildTransform(bool media)
{
    QFormLayout *f;
    section(media ? "Transform" : "Position & size", &f);
    QComboBox *fit = nullptr;
    if (media) {
        fit = new QComboBox;
        fit->addItem("Fit (letterbox)", "fit");
        fit->addItem("Fill (crop to frame)", "fill");
        fit->addItem("Stretch", "stretch");
        f->addRow("Frame", fit);
    }
    auto *scale = new SliderSpin(0.05, 5.0, 0.01, 2, "×");
    f->addRow("Scale", scale);
    auto *px = media ? new SliderSpin(-1.0, 1.0, 0.005, 3) : new SliderSpin(0.0, 1.0, 0.005, 3);
    f->addRow("Position X", px);
    auto *py = media ? new SliderSpin(-1.0, 1.0, 0.005, 3) : new SliderSpin(0.0, 1.0, 0.005, 3);
    f->addRow("Position Y", py);
    auto *rot = new SliderSpin(-180, 180, 0.5, 1, "°");
    f->addRow("Rotation", rot);
    QCheckBox *fh = nullptr, *fv = nullptr;
    if (media) {
        auto *rl = new QPushButton("⟲ 90°");
        auto *rr = new QPushButton("⟳ 90°");
        fh = new QCheckBox("Mirror");
        fv = new QCheckBox("Flip");
        f->addRow("", flowRow({rl, rr, fh, fv}));
        connect(rl, &QPushButton::clicked, this, [this]() {
            editAnimated("Rotate", "rot90", [](AnimatedValues &v) { v.rotation = std::remainder(v.rotation - 90, 360.0); });
        });
        connect(rr, &QPushButton::clicked, this, [this]() {
            editAnimated("Rotate", "rot90", [](AnimatedValues &v) { v.rotation = std::remainder(v.rotation + 90, 360.0); });
        });
        connect(fh, &QCheckBox::toggled, this, [this](bool on) { edit("Mirror", "flipH", [on](Clip &c, Track &) { c.transform.flipH = on; }); });
        connect(fv, &QCheckBox::toggled, this, [this](bool on) { edit("Flip", "flipV", [on](Clip &c, Track &) { c.transform.flipV = on; }); });
    }
    auto *op = new SliderSpin(0, 1, 0.01, 2);
    f->addRow("Opacity", op);
    auto *reset = new QPushButton("Reset");
    auto *keyHint = hint("");
    f->addRow("", reset);
    f->addRow("", keyHint);

    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const AnimatedValues v = c->animatedAt(m_doc->playhead()).baseValues();
        if (fit)
            fit->setCurrentIndex(std::max(0, fit->findData(c->transform.fit)));
        scale->setValue(v.scale);
        px->setValue(v.x);
        py->setValue(v.y);
        rot->setValue(std::remainder(v.rotation, 360.0));
        if (fh)
            fh->setChecked(c->transform.flipH);
        if (fv)
            fv->setChecked(c->transform.flipV);
        op->setValue(v.opacity);
        keyHint->setVisible(!c->fx.keyframes.isEmpty());
        keyHint->setText("◆ This clip is animated: changes here set the keyframe at the playhead.");
    };
    if (fit)
        connect(fit, &QComboBox::currentIndexChanged, this, [this, fit](int) {
            const QString v = fit->currentData().toString();
            edit("Fit", "fit", [v](Clip &c, Track &) { c.transform.fit = v; });
        });
    connect(scale, &SliderSpin::valueChanged, this, [this](double val) { editAnimated("Scale", "scale", [val](AnimatedValues &v) { v.scale = val; }); });
    connect(px, &SliderSpin::valueChanged, this, [this](double val) { editAnimated("Position", "posX", [val](AnimatedValues &v) { v.x = val; }); });
    connect(py, &SliderSpin::valueChanged, this, [this](double val) { editAnimated("Position", "posY", [val](AnimatedValues &v) { v.y = val; }); });
    connect(op, &SliderSpin::valueChanged, this, [this](double val) { editAnimated("Opacity", "opacity", [val](AnimatedValues &v) { v.opacity = val; }); });
    connect(rot, &SliderSpin::valueChanged, this, [this](double val) {
        // signed angles, so keyframes from -10° to +10° turn 20°, not 340°
        editAnimated("Rotate", "rotation", [val](AnimatedValues &v) { v.rotation = val; });
    });
    connect(reset, &QPushButton::clicked, this, [this, media]() {
        edit("Reset transform", "reset", [media](Clip &c, Track &) {
            c.fx.keyframes.clear();
            if (media) {
                const Transform keepCrop = c.transform;
                c.transform = Transform();
                c.transform.cropL = keepCrop.cropL;
                c.transform.cropT = keepCrop.cropT;
                c.transform.cropR = keepCrop.cropR;
                c.transform.cropB = keepCrop.cropB;
            } else {
                c.transform.scale = 1;
                c.transform.rotation = 0;
                c.transform.opacity = 1;
            }
        });
    });
}

void InspectorWidget::buildCrop()
{
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
        const Clip *c = firstClip();
        if (!c)
            return;
        cl->setValue(c->transform.cropL);
        ct->setValue(c->transform.cropT);
        cr->setValue(c->transform.cropR);
        cb->setValue(c->transform.cropB);
    };
    auto bindT = [this](SliderSpin *s, const QString &key, double Transform::*member) {
        connect(s, &SliderSpin::valueChanged, this, [this, key, member](double v) {
            edit("Crop", key, [member, v](Clip &c, Track &) { c.transform.*member = v; });
        });
    };
    bindT(cl, "cropL", &Transform::cropL);
    bindT(ct, "cropT", &Transform::cropT);
    bindT(cr, "cropR", &Transform::cropR);
    bindT(cb, "cropB", &Transform::cropB);
}

void InspectorWidget::buildColour()
{
    QFormLayout *f;
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
        const Clip *c = firstClip();
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
            edit("Colour", key, [member, v](Clip &c, Track &) {
                c.color.*member = v;
                c.fx.look.clear();
            });
        });
    };
    bindC(ex, "exposure", &ColorAdjust::exposure);
    bindC(co, "contrast", &ColorAdjust::contrast);
    bindC(sa, "saturation", &ColorAdjust::saturation);
    bindC(te, "temperature", &ColorAdjust::temperature);
    bindC(ti, "tint", &ColorAdjust::tint);
    connect(reset, &QPushButton::clicked, this, [this]() {
        edit("Reset colour", "colorreset", [](Clip &c, Track &) {
            c.color = ColorAdjust();
            c.fx.look.clear();
        });
    });
}

void InspectorWidget::buildTransition()
{
    QFormLayout *f;
    section("Transition in", &f);
    auto *type = new QComboBox;
    type->addItem("None (cut)", "");
    QString group;
    for (const auto &t : ImageFx::transitions())
        type->addItem(t.label, t.id);
    f->addRow("Type", type);
    auto *dur = new QDoubleSpinBox;
    dur->setRange(0.1, 5);
    dur->setSingleStep(0.1);
    dur->setDecimals(2);
    dur->setSuffix(" s");
    dur->setKeyboardTracking(false);
    f->addRow("Duration", dur);
    auto *note = hint("");
    f->addRow("", note);
    m_refreshers << [=]() {
        const Track *tr = nullptr;
        const Clip *c = firstClip();
        if (!c)
            return;
        m_doc->project().findClip(c->id, &tr);
        type->setCurrentIndex(std::max(0, type->findData(c->fx.transitionIn.type)));
        dur->setValue(c->fx.transitionIn.duration / 1e6);
        bool hasPrev = false;
        if (tr) {
            const int i = tr->indexOfClip(c->id);
            hasPrev = i > 0 && std::llabs(tr->clips[i - 1].end() - c->start) <= frameDurationUs(m_doc->project().canvas.fps);
        }
        note->setText(hasPrev ? "Plays across the cut from the previous clip."
                              : "Place this clip right after another clip on the same track to see the transition.");
    };
    connect(type, &QComboBox::currentIndexChanged, this, [this, type](int) {
        const QString v = type->currentData().toString();
        edit("Transition", "transition", [v](Clip &c, Track &) { c.fx.transitionIn.type = v; });
    });
    connect(dur, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit("Transition duration", "transdur", [v](Clip &c, Track &) { c.fx.transitionIn.duration = TimeUs(v * kUsPerSec); });
    });
}

// ------------------------------------------------------------------ keyframes

void InspectorWidget::buildKeyframes()
{
    QFormLayout *f;
    section("Keyframes", &f);
    auto *status = new QLabel;
    status->setWordWrap(true);
    f->addRow(status);
    auto *add = new QPushButton("◆ Add keyframe");
    add->setToolTip("Records position, scale, rotation and opacity at the playhead");
    auto *remove = new QPushButton("Remove");
    auto *prev = new QPushButton("◀ Previous");
    auto *next = new QPushButton("Next ▶");
    auto *clear = new QPushButton("Clear all");
    f->addRow(flowRow({add, remove, prev, next, clear}));
    auto *ease = new QComboBox;
    for (const QString &e : Keyframes::easeNames())
        ease->addItem(prettyName(e), e);
    ease->setToolTip("How the motion leaves this keyframe towards the next one");
    f->addRow("Curve", ease);
    auto *presets = new QComboBox;
    presets->addItem("Quick animation…", "");
    presets->addItem("Zoom in slowly", "zoomIn");
    presets->addItem("Zoom out slowly", "zoomOut");
    presets->addItem("Pan left → right", "panLR");
    presets->addItem("Pan right → left", "panRL");
    presets->addItem("Pop in", "popIn");
    presets->addItem("Fade in", "fadeIn");
    presets->addItem("Fade out", "fadeOut");
    presets->addItem("Spin in", "spinIn");
    presets->addItem("Rise up", "riseUp");
    f->addRow("Preset", presets);
    f->addRow(hint("Tip: with keyframes on a clip, dragging it on the video or changing Transform sets the keyframe at the playhead."));

    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const TimeUs local = m_doc->playhead() - c->start;
        const int idx = Keyframes::indexAt(c->fx.keyframes, local, 20 * kUsPerMs);
        if (c->fx.keyframes.isEmpty())
            status->setText("No keyframes. Move the playhead, set the look, then add a keyframe; repeat at another time.");
        else
            status->setText(QString("%1 keyframe%2%3")
                                .arg(c->fx.keyframes.size())
                                .arg(c->fx.keyframes.size() == 1 ? "" : "s")
                                .arg(idx >= 0 ? " · on a keyframe" : ""));
        add->setText(idx >= 0 ? "◆ Update keyframe" : "◆ Add keyframe");
        remove->setEnabled(idx >= 0);
        prev->setEnabled(!c->fx.keyframes.isEmpty());
        next->setEnabled(!c->fx.keyframes.isEmpty());
        clear->setEnabled(!c->fx.keyframes.isEmpty());
        int easeIdx = idx;
        if (easeIdx < 0)
            for (int i = 0; i < c->fx.keyframes.size(); ++i)
                if (c->fx.keyframes[i].t <= local)
                    easeIdx = i;
        ease->setEnabled(easeIdx >= 0);
        if (easeIdx >= 0)
            ease->setCurrentIndex(std::max(0, ease->findData(c->fx.keyframes[easeIdx].ease)));
    };
    connect(add, &QPushButton::clicked, this, [this]() {
        const TimeUs ph = m_doc->playhead();
        edit("Keyframe", "kfadd", [ph](Clip &c, Track &) {
            const TimeUs local = std::clamp<TimeUs>(ph - c.start, 0, c.duration());
            const AnimatedValues v = c.animatedAt(c.start + local).baseValues();
            const int idx = Keyframes::indexAt(c.fx.keyframes, local, 20 * kUsPerMs);
            if (idx >= 0) {
                Keyframe &k = c.fx.keyframes[idx];
                k.x = v.x;
                k.y = v.y;
                k.scale = v.scale;
                k.rotation = v.rotation;
                k.opacity = v.opacity;
            } else {
                c.fx.keyframes << Keyframe{local, v.x, v.y, v.scale, v.rotation, v.opacity, "smooth"};
                Keyframes::sort(c.fx.keyframes);
            }
        });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const TimeUs ph = m_doc->playhead();
        edit("Remove keyframe", "kfdel", [ph](Clip &c, Track &) {
            const int idx = Keyframes::indexAt(c.fx.keyframes, ph - c.start, 20 * kUsPerMs);
            if (idx >= 0) {
                const AnimatedValues v = Keyframes::evaluate(c.fx.keyframes, ph - c.start);
                c.fx.keyframes.removeAt(idx);
                if (c.fx.keyframes.isEmpty())
                    c.setBaseValues(v); // keep the picture where it was
            }
        });
    });
    auto jump = [this](int dir) {
        const Clip *c = firstClip();
        if (!c || c->fx.keyframes.isEmpty())
            return;
        const TimeUs local = m_doc->playhead() - c->start;
        TimeUs target = -1;
        if (dir > 0) {
            for (const Keyframe &k : c->fx.keyframes)
                if (k.t > local + 1000) {
                    target = k.t;
                    break;
                }
        } else {
            for (int i = c->fx.keyframes.size() - 1; i >= 0; --i)
                if (c->fx.keyframes[i].t < local - 1000) {
                    target = c->fx.keyframes[i].t;
                    break;
                }
        }
        if (target >= 0)
            m_doc->setPlayhead(c->start + target);
    };
    connect(prev, &QPushButton::clicked, this, [jump]() { jump(-1); });
    connect(next, &QPushButton::clicked, this, [jump]() { jump(1); });
    connect(clear, &QPushButton::clicked, this, [this]() {
        const TimeUs ph = m_doc->playhead();
        edit("Clear keyframes", "kfclear", [ph](Clip &c, Track &) {
            const AnimatedValues v = c.animatedAt(ph).baseValues();
            c.fx.keyframes.clear();
            c.setBaseValues(v);
        });
    });
    connect(ease, &QComboBox::currentIndexChanged, this, [this, ease](int) {
        const QString e = ease->currentData().toString();
        const TimeUs ph = m_doc->playhead();
        edit("Keyframe curve", "kfease", [e, ph](Clip &c, Track &) {
            const TimeUs local = ph - c.start;
            int idx = Keyframes::indexAt(c.fx.keyframes, local, 20 * kUsPerMs);
            if (idx < 0)
                for (int i = 0; i < c.fx.keyframes.size(); ++i)
                    if (c.fx.keyframes[i].t <= local)
                        idx = i;
            if (idx >= 0)
                c.fx.keyframes[idx].ease = e;
        });
    });
    connect(presets, &QComboBox::activated, this, [this, presets](int i) {
        const QString id = presets->itemData(i).toString();
        presets->setCurrentIndex(0);
        if (id.isEmpty())
            return;
        edit("Animation preset", "kfpreset", [id](Clip &c, Track &) {
            const AnimatedValues b = c.fx.keyframes.isEmpty() ? c.baseValues() : Keyframes::evaluate(c.fx.keyframes, 0);
            const TimeUs d = c.duration();
            const TimeUs quick = std::min<TimeUs>(600 * kUsPerMs, d / 2);
            const bool media = c.type == ClipType::Media;
            auto K = [&](TimeUs t, AnimatedValues v, QString e = "smooth") { return Keyframe{t, v.x, v.y, v.scale, v.rotation, v.opacity, e}; };
            AnimatedValues a = b, z = b;
            QVector<Keyframe> keys;
            if (id == "zoomIn") {
                z.scale = b.scale * 1.25;
                keys = {K(0, a, "linear"), K(d, z)};
            } else if (id == "zoomOut") {
                a.scale = b.scale * 1.25;
                keys = {K(0, a, "linear"), K(d, z)};
            } else if (id == "panLR" || id == "panRL") {
                const double off = media ? 0.08 : 0.1;
                a.x = b.x + (id == "panLR" ? -off : off);
                z.x = b.x + (id == "panLR" ? off : -off);
                a.scale = z.scale = std::max(b.scale, media ? 1.2 : b.scale);
                keys = {K(0, a, "linear"), K(d, z)};
            } else if (id == "popIn") {
                a.scale = b.scale * 0.2;
                a.opacity = 0;
                keys = {K(0, a, "bounce"), K(quick, z)};
            } else if (id == "fadeIn") {
                a.opacity = 0;
                keys = {K(0, a), K(quick, z)};
            } else if (id == "fadeOut") {
                z.opacity = 0;
                keys = {K(d - quick, a), K(d, z)};
            } else if (id == "spinIn") {
                a.rotation = b.rotation - 180;
                a.scale = b.scale * 0.3;
                a.opacity = 0;
                keys = {K(0, a, "easeOut"), K(quick, z)};
            } else if (id == "riseUp") {
                a.y = b.y + (media ? 0.15 : 0.12);
                a.opacity = 0;
                keys = {K(0, a, "easeOut"), K(quick, z)};
            }
            c.fx.keyframes = keys;
        });
    });
}

// ------------------------------------------------------------------ effects & filters

void InspectorWidget::buildEffects(bool looks)
{
    QFormLayout *f;
    if (looks) {
        section("Filter", &f);
        auto *look = new QComboBox;
        for (const auto &l : ImageFx::looks())
            look->addItem(l.label, l.id);
        f->addRow("Look", look);
        m_refreshers << [=]() {
            const Clip *c = firstClip();
            if (c)
                look->setCurrentIndex(std::max(0, look->findData(c->fx.look.isEmpty() ? QString("none") : c->fx.look)));
        };
        connect(look, &QComboBox::activated, this, [this, look](int) {
            const QString id = look->currentData().toString();
            for (const auto &l : ImageFx::looks())
                if (l.id == id)
                    edit("Filter", "look", [l](Clip &c, Track &) {
                        c.color = l.color;
                        const QString lut = c.fx.adjust.lut;
                        const double ls = c.fx.adjust.lutStrength;
                        c.fx.adjust = l.adjust;
                        c.fx.adjust.lut = lut;
                        c.fx.adjust.lutStrength = ls;
                        c.fx.look = l.id == "none" ? QString() : l.id;
                    });
        });

        section("Adjust", &f);
        auto *hue = new SliderSpin(-180, 180, 1, 0, "°");
        auto *vib = new SliderSpin(-1, 1, 0.01, 2);
        auto *hi = new SliderSpin(-1, 1, 0.01, 2);
        auto *sh = new SliderSpin(-1, 1, 0.01, 2);
        auto *fade = new SliderSpin(0, 1, 0.01, 2);
        auto *vig = new SliderSpin(0, 1, 0.01, 2);
        auto *grain = new SliderSpin(0, 1, 0.01, 2);
        auto *sharp = new SliderSpin(0, 1, 0.01, 2);
        f->addRow("Hue", hue);
        f->addRow("Vibrance", vib);
        f->addRow("Highlights", hi);
        f->addRow("Shadows", sh);
        f->addRow("Fade", fade);
        f->addRow("Vignette", vig);
        f->addRow("Grain", grain);
        f->addRow("Sharpen", sharp);
        auto *lutLabel = new QLabel;
        lutLabel->setWordWrap(true);
        auto *lutLoad = new QPushButton("Load LUT (.cube)…");
        auto *lutClear = new QPushButton("Remove LUT");
        auto *lutStrength = new SliderSpin(0, 1, 0.01, 2);
        f->addRow("LUT", lutLabel);
        f->addRow("", flowRow({lutLoad, lutClear}));
        f->addRow("LUT strength", lutStrength);
        auto *resetAdj = new QPushButton("Reset adjustments");
        f->addRow("", resetAdj);
        m_refreshers << [=]() {
            const Clip *c = firstClip();
            if (!c)
                return;
            const ExtraAdjust &a = c->fx.adjust;
            hue->setValue(a.hue);
            vib->setValue(a.vibrance);
            hi->setValue(a.highlights);
            sh->setValue(a.shadows);
            fade->setValue(a.fade);
            vig->setValue(a.vignette);
            grain->setValue(a.grain);
            sharp->setValue(a.sharpen);
            lutLabel->setText(a.lut.isEmpty() ? QString("None") : QFileInfo(a.lut).fileName());
            lutClear->setEnabled(!a.lut.isEmpty());
            lutStrength->setEnabled(!a.lut.isEmpty());
            lutStrength->setValue(a.lutStrength);
        };
        auto bindA = [this](SliderSpin *s, const QString &key, double ExtraAdjust::*m) {
            connect(s, &SliderSpin::valueChanged, this, [this, key, m](double v) {
                edit("Adjust", key, [m, v](Clip &c, Track &) { c.fx.adjust.*m = v; });
            });
        };
        bindA(hue, "hue", &ExtraAdjust::hue);
        bindA(vib, "vibrance", &ExtraAdjust::vibrance);
        bindA(hi, "highlights", &ExtraAdjust::highlights);
        bindA(sh, "shadows", &ExtraAdjust::shadows);
        bindA(fade, "fade", &ExtraAdjust::fade);
        bindA(vig, "vignette", &ExtraAdjust::vignette);
        bindA(grain, "grain", &ExtraAdjust::grain);
        bindA(sharp, "sharpen", &ExtraAdjust::sharpen);
        bindA(lutStrength, "lutStrength", &ExtraAdjust::lutStrength);
        connect(lutLoad, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(this, "Load LUT", QString(), "3D LUT (*.cube)");
            if (path.isEmpty())
                return;
            QImage probe(4, 4, QImage::Format_ARGB32_Premultiplied);
            probe.fill(Qt::gray);
            QString err;
            if (!ImageFx::applyLut(probe, path, 1.0, &err)) {
                emit statusMessage(err);
                return;
            }
            edit("Load LUT", "lut", [path](Clip &c, Track &) {
                c.fx.adjust.lut = path;
                c.fx.adjust.lutStrength = 1.0;
            });
        });
        connect(lutClear, &QPushButton::clicked, this, [this]() { edit("Remove LUT", "lut", [](Clip &c, Track &) { c.fx.adjust.lut.clear(); }); });
        connect(resetAdj, &QPushButton::clicked, this, [this]() {
            edit("Reset adjustments", "adjreset", [](Clip &c, Track &) {
                c.fx.adjust = ExtraAdjust();
                c.fx.look.clear();
            });
        });
    }

    section("Video effects", &f);
    auto *list = new QListWidget;
    list->setMinimumHeight(90);
    list->setMaximumHeight(150);
    f->addRow(list);
    auto *add = new QComboBox;
    add->addItem("Add effect…", "");
    for (const auto &e : ImageFx::effects())
        add->addItem(QString("%1 · %2").arg(e.group, e.label), e.id);
    f->addRow(add);
    auto *intensity = new SliderSpin(0, 1, 0.01, 2);
    auto *speed = new SliderSpin(0.1, 5, 0.05, 2, "×");
    f->addRow("Intensity", intensity);
    f->addRow("Speed", speed);
    auto *up = new QPushButton("Move up");
    auto *down = new QPushButton("Move down");
    auto *removeFx = new QPushButton("Remove");
    f->addRow(flowRow({up, down, removeFx}));
    f->addRow(hint("Effects run in list order. Tip: drag an effect from the Library onto a clip, or onto empty "
                   "timeline space to make an effect layer that affects everything below it."));

    auto selected = [list]() { return list->currentRow(); };
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const int row = list->currentRow();
        list->blockSignals(true);
        list->clear();
        for (const EffectSpec &e : c->fx.effects)
            list->addItem(QString("%1  ·  %2%").arg(ImageFx::effectLabel(e.type)).arg(int(std::lround(e.intensity * 100))));
        list->setCurrentRow(std::min(row < 0 ? 0 : row, int(c->fx.effects.size()) - 1));
        list->blockSignals(false);
        const int r = list->currentRow();
        const bool has = r >= 0 && r < c->fx.effects.size();
        intensity->setEnabled(has);
        speed->setEnabled(has);
        removeFx->setEnabled(has);
        up->setEnabled(has && r > 0);
        down->setEnabled(has && r + 1 < c->fx.effects.size());
        if (has) {
            intensity->setValue(c->fx.effects[r].intensity);
            speed->setValue(c->fx.effects[r].speed);
        }
    };
    connect(list, &QListWidget::currentRowChanged, this, [this](int) { refreshValues(); });
    connect(add, &QComboBox::activated, this, [this, add](int i) {
        const QString id = add->itemData(i).toString();
        add->setCurrentIndex(0);
        if (!id.isEmpty())
            edit("Add effect", "fxadd", [id](Clip &c, Track &) { c.fx.effects << EffectSpec{id, 0.6, 1.0}; });
    });
    connect(intensity, &SliderSpin::valueChanged, this, [this, selected](double v) {
        const int r = selected();
        edit("Effect intensity", QString("fxint%1").arg(r), [r, v](Clip &c, Track &) {
            if (r >= 0 && r < c.fx.effects.size())
                c.fx.effects[r].intensity = v;
        });
    });
    connect(speed, &SliderSpin::valueChanged, this, [this, selected](double v) {
        const int r = selected();
        edit("Effect speed", QString("fxspd%1").arg(r), [r, v](Clip &c, Track &) {
            if (r >= 0 && r < c.fx.effects.size())
                c.fx.effects[r].speed = v;
        });
    });
    connect(removeFx, &QPushButton::clicked, this, [this, selected]() {
        const int r = selected();
        edit("Remove effect", "fxdel", [r](Clip &c, Track &) {
            if (r >= 0 && r < c.fx.effects.size())
                c.fx.effects.removeAt(r);
        });
    });
    auto moveFx = [this, selected, list](int dir) {
        const int r = selected();
        edit("Reorder effects", "fxmove", [r, dir](Clip &c, Track &) {
            const int to = r + dir;
            if (r >= 0 && to >= 0 && to < c.fx.effects.size())
                c.fx.effects.swapItemsAt(r, to);
        });
        list->setCurrentRow(r + dir);
    };
    connect(up, &QPushButton::clicked, this, [moveFx]() { moveFx(-1); });
    connect(down, &QPushButton::clicked, this, [moveFx]() { moveFx(1); });

    section("Blend", &f);
    auto *blend = new QComboBox;
    for (const QString &b : ImageFx::blendModes())
        blend->addItem(prettyName(b), b);
    f->addRow("Mode", blend);
    m_refreshers << [=]() {
        if (const Clip *c = firstClip())
            blend->setCurrentIndex(std::max(0, blend->findData(c->fx.blend)));
    };
    connect(blend, &QComboBox::currentIndexChanged, this, [this, blend](int) {
        const QString b = blend->currentData().toString();
        edit("Blend mode", "blend", [b](Clip &c, Track &) { c.fx.blend = b; });
    });
}

// ------------------------------------------------------------------ cutout

void InspectorWidget::buildCutout()
{
    QFormLayout *f;
    section("Chroma key (green screen)", &f);
    auto *on = new QCheckBox("Remove a colour");
    f->addRow(on);
    auto *color = new ColorButton;
    auto *green = new QPushButton("Green");
    auto *blue = new QPushButton("Blue");
    auto *row = new QHBoxLayout;
    row->addWidget(color);
    row->addWidget(green);
    row->addWidget(blue);
    row->addStretch();
    f->addRow("Key colour", row);
    auto *sim = new SliderSpin(0, 1, 0.01, 2);
    auto *smooth = new SliderSpin(0, 1, 0.01, 2);
    auto *spill = new SliderSpin(0, 1, 0.01, 2);
    f->addRow("Strength", sim);
    f->addRow("Edge softness", smooth);
    f->addRow("Spill removal", spill);
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        on->setChecked(c->fx.chroma.enabled);
        color->setColor(c->fx.chroma.color);
        sim->setValue(c->fx.chroma.similarity);
        smooth->setValue(c->fx.chroma.smoothness);
        spill->setValue(c->fx.chroma.spill);
        for (QWidget *w : QList<QWidget *>{color, green, blue, sim, smooth, spill})
            w->setEnabled(c->fx.chroma.enabled);
    };
    connect(on, &QCheckBox::toggled, this, [this](bool v) { edit("Chroma key", "chroma", [v](Clip &c, Track &) { c.fx.chroma.enabled = v; }); });
    connect(color, &ColorButton::colorChanged, this, [this](const QColor &col) { edit("Key colour", "chromacol", [col](Clip &c, Track &) { c.fx.chroma.color = col; }); });
    connect(green, &QPushButton::clicked, this, [this]() { edit("Key colour", "chromacol", [](Clip &c, Track &) { c.fx.chroma.color = QColor(0, 255, 0); }); });
    connect(blue, &QPushButton::clicked, this, [this]() { edit("Key colour", "chromacol", [](Clip &c, Track &) { c.fx.chroma.color = QColor(0, 70, 255); }); });
    connect(sim, &SliderSpin::valueChanged, this, [this](double v) { edit("Key strength", "chromasim", [v](Clip &c, Track &) { c.fx.chroma.similarity = v; }); });
    connect(smooth, &SliderSpin::valueChanged, this, [this](double v) { edit("Key softness", "chromasm", [v](Clip &c, Track &) { c.fx.chroma.smoothness = v; }); });
    connect(spill, &SliderSpin::valueChanged, this, [this](double v) { edit("Spill", "chromasp", [v](Clip &c, Track &) { c.fx.chroma.spill = v; }); });

    section("Mask", &f);
    auto *type = new QComboBox;
    for (const auto &[l, v] : QVector<QPair<QString, QString>>{{"None", ""}, {"Linear (split)", "linear"}, {"Mirror (band)", "mirror"}, {"Circle", "circle"}, {"Rectangle", "rect"}})
        type->addItem(l, v);
    f->addRow("Shape", type);
    auto *cx = new SliderSpin(0, 1, 0.005, 3);
    auto *cy = new SliderSpin(0, 1, 0.005, 3);
    auto *mw = new SliderSpin(0.01, 1.5, 0.005, 3);
    auto *mh = new SliderSpin(0.01, 1.5, 0.005, 3);
    auto *ang = new SliderSpin(-180, 180, 1, 0, "°");
    auto *feather = new SliderSpin(0, 0.5, 0.005, 3);
    auto *radius = new SliderSpin(0, 0.5, 0.01, 2);
    auto *invert = new QCheckBox("Invert");
    f->addRow("Centre X", cx);
    f->addRow("Centre Y", cy);
    f->addRow("Width", mw);
    f->addRow("Height", mh);
    f->addRow("Angle", ang);
    f->addRow("Feather", feather);
    f->addRow("Corner radius", radius);
    f->addRow("", invert);
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const MaskSpec &m = c->fx.mask;
        type->setCurrentIndex(std::max(0, type->findData(m.type)));
        cx->setValue(m.cx);
        cy->setValue(m.cy);
        mw->setValue(m.w);
        mh->setValue(m.h);
        ang->setValue(m.angle);
        feather->setValue(m.feather);
        radius->setValue(m.radius);
        invert->setChecked(m.invert);
        for (QWidget *w : QList<QWidget *>{cx, cy, mw, mh, ang, feather, radius, invert})
            w->setEnabled(m.isSet());
        mw->setEnabled(m.type == "circle" || m.type == "rect");
        mh->setEnabled(m.type == "circle" || m.type == "rect" || m.type == "mirror");
        radius->setEnabled(m.type == "rect");
    };
    connect(type, &QComboBox::currentIndexChanged, this, [this, type](int) {
        const QString t = type->currentData().toString();
        edit("Mask", "masktype", [t](Clip &c, Track &) {
            c.fx.mask.type = t;
            if (t == "mirror" && c.fx.mask.h > 0.5)
                c.fx.mask.h = 0.3;
        });
    });
    auto bindM = [this](SliderSpin *s, const QString &key, double MaskSpec::*m) {
        connect(s, &SliderSpin::valueChanged, this, [this, key, m](double v) { edit("Mask", key, [m, v](Clip &c, Track &) { c.fx.mask.*m = v; }); });
    };
    bindM(cx, "maskcx", &MaskSpec::cx);
    bindM(cy, "maskcy", &MaskSpec::cy);
    bindM(mw, "maskw", &MaskSpec::w);
    bindM(mh, "maskh", &MaskSpec::h);
    bindM(ang, "maskang", &MaskSpec::angle);
    bindM(feather, "maskfeather", &MaskSpec::feather);
    bindM(radius, "maskradius", &MaskSpec::radius);
    connect(invert, &QCheckBox::toggled, this, [this](bool v) { edit("Mask", "maskinv", [v](Clip &c, Track &) { c.fx.mask.invert = v; }); });

    section("Canvas background", &f);
    auto *mode = new QComboBox;
    mode->addItem("None", "");
    mode->addItem("Blurred video", "blur");
    mode->addItem("Colour", "color");
    f->addRow("Fill", mode);
    auto *blur = new SliderSpin(0, 1, 0.01, 2);
    auto *bgc = new ColorButton;
    f->addRow("Blur", blur);
    f->addRow("Colour", bgc);
    auto *allClips = new QPushButton("Apply to all video clips");
    f->addRow("", allClips);
    f->addRow(hint("Fills the empty space around the video, e.g. a landscape clip in a 9:16 project."));
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        mode->setCurrentIndex(std::max(0, mode->findData(c->fx.background.mode)));
        blur->setValue(c->fx.background.blur);
        bgc->setColor(c->fx.background.color);
        blur->setEnabled(c->fx.background.mode == "blur");
        bgc->setEnabled(c->fx.background.mode == "color");
    };
    connect(mode, &QComboBox::currentIndexChanged, this, [this, mode](int) {
        const QString m = mode->currentData().toString();
        edit("Canvas background", "bgmode", [m](Clip &c, Track &) { c.fx.background.mode = m; });
    });
    connect(blur, &SliderSpin::valueChanged, this, [this](double v) { edit("Background blur", "bgblur", [v](Clip &c, Track &) { c.fx.background.blur = v; }); });
    connect(bgc, &ColorButton::colorChanged, this, [this](const QColor &col) { edit("Background colour", "bgcol", [col](Clip &c, Track &) { c.fx.background.color = col; }); });
    connect(allClips, &QPushButton::clicked, this, [this]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const BackgroundFill bg = c->fx.background;
        m_doc->apply("Background for all clips", [bg](Project &p) {
            for (auto &t : p.tracks)
                if (t.kind == TrackKind::Video && !t.locked)
                    for (auto &cl : t.clips)
                        if (cl.type == ClipType::Media)
                            cl.fx.background = bg;
            return true;
        });
    });
}

// ------------------------------------------------------------------ speed

void InspectorWidget::buildSpeed(bool hasSpeed, bool video)
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    if (hasSpeed) {
        section("Speed", &f);
        auto *speed = new QDoubleSpinBox;
        speed->setRange(0.1, 16);
        speed->setSingleStep(0.05);
        speed->setDecimals(2);
        speed->setSuffix("×");
        speed->setKeyboardTracking(false);
        f->addRow("Speed", speed);
        auto *quick = new QHBoxLayout;
        for (double v : {0.25, 0.5, 1.0, 2.0, 4.0}) {
            auto *b = new QPushButton(QString("%1×").arg(v));
            b->setMaximumWidth(64);
            quick->addWidget(b);
            connect(b, &QPushButton::clicked, speed, [speed, v]() { speed->setValue(v); });
        }
        quick->addStretch();
        f->addRow("", quick);
        f->addRow(hint("Linked audio follows (pitch-preserving). Captions stay in sync."));
        m_refreshers << [=]() {
            if (const Clip *c = firstClip())
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
    if (!video)
        return;
    section("Speed curve", &f);
    auto *curve = new QComboBox;
    curve->addItem("None", "");
    for (const QString &c : SpeedCurve::presetNames())
        curve->addItem(prettyName(c), c);
    f->addRow("Curve", curve);
    f->addRow(hint("Slow-motion and fast-forward ramps inside the clip; its length stays the same. "
                   "The clip's sound is muted while a curve is on."));
    m_refreshers << [=]() {
        if (const Clip *c = firstClip())
            curve->setCurrentIndex(std::max(0, curve->findData(c->fx.speedCurve.isSet() ? c->fx.speedCurve.preset : QString())));
    };
    connect(curve, &QComboBox::activated, this, [this, curve](int) {
        const QString id = curve->currentData().toString();
        edit("Speed curve", "curve", [id](Clip &c, Track &) { c.fx.speedCurve = id.isEmpty() ? SpeedCurve() : SpeedCurve::fromPreset(id); });
    });

    section("Tools", &f);
    auto *rev = new QPushButton("Reverse");
    rev->setToolTip("Plays the clip backwards (renders a reversed copy on this computer)");
    auto *freeze = new QPushButton("Freeze frame");
    freeze->setToolTip("Inserts a 3-second still of the frame under the playhead");
    auto *stab = new QPushButton("Stabilise");
    stab->setToolTip("Smooths shaky footage (renders a stabilised copy on this computer)");
    auto *extract = new QPushButton("Extract audio");
    extract->setToolTip("Saves the clip's sound as a separate audio file and adds it to the media bin");
    f->addRow(flowRow({rev, freeze, stab, extract}));
    connect(rev, &QPushButton::clicked, this, [this, ids]() { emit reverseRequested(ids.first()); });
    connect(freeze, &QPushButton::clicked, this, [this, ids]() { emit freezeFrameRequested(ids.first()); });
    connect(stab, &QPushButton::clicked, this, [this, ids]() { emit stabilizeRequested(ids.first()); });
    connect(extract, &QPushButton::clicked, this, [this, ids]() { emit extractAudioRequested(ids.first()); });
}

// ------------------------------------------------------------------ audio

void InspectorWidget::buildAudio()
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    section("Audio", &f);
    auto *gain = new SliderSpin(-40, 20, 0.5, 1, " dB");
    f->addRow("Volume", gain);
    auto *mute = new QCheckBox("Mute clip");
    f->addRow("", mute);
    auto *tv = new SliderSpin(-40, 12, 0.5, 1, " dB");
    f->addRow("Track volume", tv);
    auto *duck = new QCheckBox("Auto-duck this track under voice");
    duck->setToolTip("Lowers this track (usually music) whenever other audio, like speech, is playing");
    f->addRow("", duck);
    m_refreshers << [=]() {
        const Track *t = nullptr;
        const Clip *c = m_doc->project().findClip(ids.first(), &t);
        if (!c)
            return;
        gain->setValue(c->audio.gainDb);
        mute->setChecked(c->audio.mute);
        tv->setValue(t->volumeDb);
        duck->setChecked(t->duck);
    };
    connect(gain, &SliderSpin::valueChanged, this, [this](double v) { edit("Volume", "gain", [v](Clip &c, Track &) { c.audio.gainDb = v; }); });
    connect(mute, &QCheckBox::toggled, this, [this](bool on) { edit("Mute", "mute", [on](Clip &c, Track &) { c.audio.mute = on; }); });
    connect(tv, &SliderSpin::valueChanged, this, [this](double v) { edit("Track volume", "trackvol", [v](Clip &, Track &t) { t.volumeDb = v; }); });
    connect(duck, &QCheckBox::toggled, this, [this](bool on) { edit("Auto-duck", "duck", [on](Clip &, Track &t) { t.duck = on; }); });
}

void InspectorWidget::buildVoice()
{
    QFormLayout *f;
    section("Voice effects", &f);
    auto *voice = new QComboBox;
    voice->addItem("None", "");
    for (const QString &v : AudioFx::voiceNames())
        voice->addItem(prettyName(v), v);
    f->addRow("Voice", voice);
    auto *denoise = new QCheckBox("Reduce noise");
    auto *enhance = new QCheckBox("Enhance voice (clarity)");
    f->addRow("", denoise);
    f->addRow("", enhance);
    f->addRow(hint("Processed on this computer with FFmpeg. The preview switches to the processed sound after a short render."));
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        voice->setCurrentIndex(std::max(0, voice->findData(c->fx.audio.voice)));
        denoise->setChecked(c->fx.audio.denoise);
        enhance->setChecked(c->fx.audio.enhance);
    };
    connect(voice, &QComboBox::currentIndexChanged, this, [this, voice](int) {
        const QString v = voice->currentData().toString();
        edit("Voice effect", "voice", [v](Clip &c, Track &) { c.fx.audio.voice = v; });
    });
    connect(denoise, &QCheckBox::toggled, this, [this](bool on) { edit("Noise reduction", "denoise", [on](Clip &c, Track &) { c.fx.audio.denoise = on; }); });
    connect(enhance, &QCheckBox::toggled, this, [this](bool on) { edit("Enhance voice", "enhance", [on](Clip &c, Track &) { c.fx.audio.enhance = on; }); });
}

void InspectorWidget::buildFades(bool audio)
{
    const QStringList ids = m_doc->selectedClips();
    QFormLayout *f;
    section(audio ? "Fades" : "Fade", &f);
    auto *fi = new QDoubleSpinBox;
    auto *fo = new QDoubleSpinBox;
    for (auto *s : {fi, fo}) {
        s->setRange(0, 30);
        s->setSingleStep(0.1);
        s->setDecimals(2);
        s->setSuffix(" s");
        s->setKeyboardTracking(false);
    }
    f->addRow("Fade in", fi);
    f->addRow("Fade out", fo);
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

void InspectorWidget::buildTitleText()
{
    // Several texts selected: say so, and offer to edit just one (edits apply to every selected text).
    int titles = 0;
    for (const QString &id : m_doc->selectedClips())
        if (const Clip *c = m_doc->project().findClip(id); c && c->type == ClipType::Title)
            ++titles;
    if (titles > 1) {
        auto *banner = new QWidget;
        auto *bl = new QHBoxLayout(banner);
        bl->setContentsMargins(8, 6, 8, 6);
        auto *lab = new QLabel(QString("Editing %1 texts together").arg(titles));
        lab->setWordWrap(true);
        auto *only = new QPushButton("Edit only this one");
        bl->addWidget(lab, 1);
        bl->addWidget(only);
        banner->setStyleSheet("background: rgba(91,140,255,0.18); border-radius: 6px;");
        m_layout->addWidget(banner);
        connect(only, &QPushButton::clicked, this, [this]() {
            if (const Clip *c = firstClip())
                m_doc->setSelectedClips({c->id});
        });
    }

    // Professional looks: one click restyles the text (keeps its words, position and size).
    QFormLayout *sf;
    section("Text styles", &sf);
    auto *styles = TextStyleUi::makeGrid(nullptr, QSize(84, 47));
    styles->setFixedHeight(2 * (47 + 26) + 10);
    styles->setToolTip("Click a style to apply it to this text only");
    sf->addRow(styles);
    connect(styles, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        edit("Text style: " + it->text(), "textstyle", [id](Clip &c, Track &) {
            if (c.type == ClipType::Title)
                TextStyles::apply(c.title, id);
        });
    });
    m_refreshers << [this, styles]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const QSignalBlocker b(styles);
        styles->clearSelection();
        for (int i = 0; i < styles->count(); ++i)
            if (styles->item(i)->data(Qt::UserRole).toString() == c->title.styleId)
                styles->item(i)->setSelected(true);
    };

    QFormLayout *f;
    section("Text", &f);
    auto *text = new QPlainTextEdit;
    text->setMaximumHeight(80);
    f->addRow(text);
    auto *font = new QFontComboBox;
    f->addRow("Font", font);
    auto *weight = new QComboBox;
    for (int w : {300, 400, 500, 600, 700, 800, 900})
        weight->addItem(QString::number(w), w);
    auto *italic = new QCheckBox("Italic");
    auto *underline = new QCheckBox("Underline");
    auto *wr = new QHBoxLayout;
    wr->addWidget(weight);
    wr->addWidget(italic);
    f->addRow("Weight", wr);
    auto *textCase = new QComboBox;
    textCase->addItem("As typed", QString());
    textCase->addItem("UPPERCASE", "upper");
    textCase->addItem("lowercase", "lower");
    textCase->addItem("Title Case", "title");
    f->addRow("Format", flowRow({underline, textCase}));
    auto *size = new SliderSpin(0.01, 0.3, 0.002, 3);
    f->addRow("Size", size);
    auto *color = new ColorButton;
    auto *outline = new ColorButton;
    auto *bg = new ColorButton;
    f->addRow("Text colour", color);
    f->addRow("Outline", outline);
    auto *ow = new SliderSpin(0, 0.3, 0.005, 3);
    f->addRow("Outline width", ow);
    f->addRow("Box", bg);
    auto *align = new QComboBox;
    align->addItem("Left", "left");
    align->addItem("Center", "center");
    align->addItem("Right", "right");
    f->addRow("Align", align);
    auto *spacing = new SliderSpin(0.7, 2.5, 0.05, 2, "×");
    f->addRow("Line spacing", spacing);
    auto *highlight = new ColorButton;
    highlight->setToolTip("Words written between *stars* use this colour");
    auto *hint = new QLabel("Put *stars* around words to colour them");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(mid);");
    f->addRow("Highlight", highlight);
    f->addRow("", hint);
    auto *lower = new QPushButton("Lower third");
    auto *center = new QPushButton("Centre");
    auto *topl = new QPushButton("Top label");
    f->addRow("Layout", flowRow({lower, center, topl}));

    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const TitleProps &t = c->title;
        if (text->toPlainText() != t.text)
            text->setPlainText(t.text);
        font->setCurrentFont(QFont(TextStyleUi::installedFamily(t.fontFamily)));
        weight->setCurrentIndex(std::max(0, weight->findData(t.weight)));
        italic->setChecked(t.italic);
        underline->setChecked(t.underline);
        textCase->setCurrentIndex(std::max(0, textCase->findData(t.textCase)));
        spacing->setValue(t.lineSpacing);
        highlight->setColor(t.highlightColor);
        size->setValue(t.sizePct);
        color->setColor(t.color);
        outline->setColor(t.outlineColor);
        bg->setColor(t.backgroundColor);
        ow->setValue(t.outlineWidthPct);
        align->setCurrentIndex(std::max(0, align->findData(t.align)));
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
    connect(underline, &QCheckBox::toggled, this, [E](bool on) { E("underline", [on](TitleProps &t) { t.underline = on; }); });
    connect(textCase, &QComboBox::currentIndexChanged, this, [textCase, E](int) {
        const QString v = textCase->currentData().toString();
        E("case", [v](TitleProps &t) { t.textCase = v; });
    });
    connect(spacing, &SliderSpin::valueChanged, this, [E](double v) { E("spacing", [v](TitleProps &t) { t.lineSpacing = v; }); });
    connect(highlight, &ColorButton::colorChanged, this, [E](const QColor &c) { E("highlight", [c](TitleProps &t) { t.highlightColor = c; }); });
    connect(size, &SliderSpin::valueChanged, this, [E](double v) { E("size", [v](TitleProps &t) { t.sizePct = v; }); });
    connect(color, &ColorButton::colorChanged, this, [E](const QColor &c) { E("color", [c](TitleProps &t) { t.color = c; }); });
    connect(outline, &ColorButton::colorChanged, this, [E](const QColor &c) { E("outline", [c](TitleProps &t) { t.outlineColor = c; }); });
    connect(bg, &ColorButton::colorChanged, this, [E](const QColor &c) { E("bg", [c](TitleProps &t) { t.backgroundColor = c; }); });
    connect(ow, &SliderSpin::valueChanged, this, [E](double v) { E("ow", [v](TitleProps &t) { t.outlineWidthPct = v; }); });
    connect(align, &QComboBox::currentIndexChanged, this, [align, E](int) { const QString a = align->currentData().toString(); E("align", [a](TitleProps &t) { t.align = a; }); });
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
}

void InspectorWidget::buildTitleStyle()
{
    QFormLayout *f;
    section("Style", &f);
    auto *grad = new ColorButton;
    auto *gradOn = new QCheckBox("Gradient");
    auto *gr = new QHBoxLayout;
    gr->addWidget(gradOn);
    gr->addWidget(grad);
    gr->addStretch();
    f->addRow("Fill", gr);
    auto *shadow = new ColorButton;
    auto *shadowOn = new QCheckBox("Shadow");
    auto *sr = new QHBoxLayout;
    sr->addWidget(shadowOn);
    sr->addWidget(shadow);
    sr->addStretch();
    f->addRow("Shadow", sr);
    auto *so = new SliderSpin(0, 0.3, 0.005, 3);
    f->addRow("Shadow distance", so);
    auto *glow = new ColorButton;
    auto *glowOn = new QCheckBox("Glow");
    auto *glr = new QHBoxLayout;
    glr->addWidget(glowOn);
    glr->addWidget(glow);
    glr->addStretch();
    f->addRow("Glow", glr);
    auto *ls = new SliderSpin(-10, 60, 0.5, 1, "%");
    f->addRow("Letter spacing", ls);
    auto *bp = new SliderSpin(0, 1.5, 0.01, 2);
    auto *br = new SliderSpin(0, 1.0, 0.01, 2);
    f->addRow("Box padding", bp);
    f->addRow("Box roundness", br);
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const TitleProps &t = c->title;
        gradOn->setChecked(t.gradientColor.alpha() > 0);
        grad->setColor(t.gradientColor.alpha() > 0 ? t.gradientColor : QColor("#FF6BD6"));
        grad->setEnabled(t.gradientColor.alpha() > 0);
        shadowOn->setChecked(t.shadowColor.alpha() > 0);
        shadow->setColor(t.shadowColor.alpha() > 0 ? t.shadowColor : QColor(0, 0, 0, 160));
        shadow->setEnabled(t.shadowColor.alpha() > 0);
        so->setValue(t.shadowOffsetPct);
        glowOn->setChecked(t.glowColor.alpha() > 0);
        glow->setColor(t.glowColor.alpha() > 0 ? t.glowColor : QColor("#22D3EE"));
        glow->setEnabled(t.glowColor.alpha() > 0);
        ls->setValue(t.letterSpacingPct);
        bp->setValue(t.boxPaddingPct);
        br->setValue(t.boxRadiusPct);
    };
    auto E = [this](const QString &key, const std::function<void(TitleProps &)> &fn) {
        edit("Text style", key, [fn](Clip &c, Track &) { fn(c.title); });
    };
    connect(gradOn, &QCheckBox::toggled, this, [E, grad](bool on) {
        const QColor c = grad->color();
        E("grad", [on, c](TitleProps &t) { t.gradientColor = on ? (c.alpha() > 0 ? c : QColor("#FF6BD6")) : QColor(0, 0, 0, 0); });
    });
    connect(grad, &ColorButton::colorChanged, this, [E](const QColor &c) { E("gradc", [c](TitleProps &t) { t.gradientColor = c; }); });
    connect(shadowOn, &QCheckBox::toggled, this, [E, shadow](bool on) {
        const QColor c = shadow->color();
        E("shadow", [on, c](TitleProps &t) { t.shadowColor = on ? (c.alpha() > 0 ? c : QColor(0, 0, 0, 160)) : QColor(0, 0, 0, 0); });
    });
    connect(shadow, &ColorButton::colorChanged, this, [E](const QColor &c) { E("shadowc", [c](TitleProps &t) { t.shadowColor = c; }); });
    connect(so, &SliderSpin::valueChanged, this, [E](double v) { E("shadowo", [v](TitleProps &t) { t.shadowOffsetPct = v; }); });
    connect(glowOn, &QCheckBox::toggled, this, [E, glow](bool on) {
        const QColor c = glow->color();
        E("glow", [on, c](TitleProps &t) { t.glowColor = on ? (c.alpha() > 0 ? c : QColor("#22D3EE")) : QColor(0, 0, 0, 0); });
    });
    connect(glow, &ColorButton::colorChanged, this, [E](const QColor &c) { E("glowc", [c](TitleProps &t) { t.glowColor = c; }); });
    connect(ls, &SliderSpin::valueChanged, this, [E](double v) { E("ls", [v](TitleProps &t) { t.letterSpacingPct = v; }); });
    connect(bp, &SliderSpin::valueChanged, this, [E](double v) { E("bp", [v](TitleProps &t) { t.boxPaddingPct = v; }); });
    connect(br, &SliderSpin::valueChanged, this, [E](double v) { E("br", [v](TitleProps &t) { t.boxRadiusPct = v; }); });
}

void InspectorWidget::buildTitleAnimation()
{
    QFormLayout *f;
    section("Text animation", &f);
    auto mk = [](const QStringList &ids) {
        auto *c = new QComboBox;
        for (const QString &id : ids)
            c->addItem(prettyName(id), id);
        return c;
    };
    auto *in = mk(GraphicsRenderer::titleInAnimations());
    auto *out = mk(GraphicsRenderer::titleOutAnimations());
    auto *loop = mk(GraphicsRenderer::titleLoopAnimations());
    f->addRow("In", in);
    f->addRow("Out", out);
    f->addRow("Loop", loop);
    auto *ad = new QDoubleSpinBox;
    ad->setRange(0, 5);
    ad->setSingleStep(0.1);
    ad->setSuffix(" s");
    ad->setKeyboardTracking(false);
    f->addRow("Duration", ad);
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        in->setCurrentIndex(std::max(0, in->findData(c->title.animation)));
        out->setCurrentIndex(std::max(0, out->findData(c->title.animationOut)));
        loop->setCurrentIndex(std::max(0, loop->findData(c->title.loop)));
        ad->setValue(c->title.animationDuration / 1e6);
    };
    auto E = [this](const QString &key, const std::function<void(TitleProps &)> &fn) {
        edit("Text animation", key, [fn](Clip &c, Track &) { fn(c.title); });
    };
    connect(in, &QComboBox::currentIndexChanged, this, [in, E](int) { const QString v = in->currentData().toString(); E("animin", [v](TitleProps &t) { t.animation = v; }); });
    connect(out, &QComboBox::currentIndexChanged, this, [out, E](int) { const QString v = out->currentData().toString(); E("animout", [v](TitleProps &t) { t.animationOut = v; }); });
    connect(loop, &QComboBox::currentIndexChanged, this, [loop, E](int) { const QString v = loop->currentData().toString(); E("loop", [v](TitleProps &t) { t.loop = v; }); });
    connect(ad, &QDoubleSpinBox::valueChanged, this, [E](double v) { E("animdur", [v](TitleProps &t) { t.animationDuration = TimeUs(v * kUsPerSec); }); });
}

// ------------------------------------------------------------------ shape

void InspectorWidget::buildShape()
{
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
    f->addRow("Fill", fill);
    f->addRow("Stroke", stroke);
    auto *sw = new SliderSpin(0, 0.05, 0.001, 3);
    f->addRow("Stroke width", sw);
    auto *w = new SliderSpin(0.005, 1.5, 0.005, 3);
    auto *h = new SliderSpin(0.002, 1.5, 0.005, 3);
    auto *r = new SliderSpin(0, 0.5, 0.01, 2);
    f->addRow("Width", w);
    f->addRow("Height", h);
    f->addRow("Corner radius", r);
    m_refreshers << [=]() {
        const Clip *c = firstClip();
        if (!c)
            return;
        const ShapeProps &s = c->shape;
        kind->setCurrentIndex(std::max(0, kind->findData(s.shape)));
        fill->setColor(s.fill);
        stroke->setColor(s.stroke);
        sw->setValue(s.strokeWidthPct);
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
    connect(w, &SliderSpin::valueChanged, this, [E](double v) { E("w", [v](ShapeProps &s) { s.w = v; }); });
    connect(h, &SliderSpin::valueChanged, this, [E](double v) { E("h", [v](ShapeProps &s) { s.h = v; }); });
    connect(r, &SliderSpin::valueChanged, this, [E](double v) { E("r", [v](ShapeProps &s) { s.radiusPct = v; }); });
}

} // namespace ss
