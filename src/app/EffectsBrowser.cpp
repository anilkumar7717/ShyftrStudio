#include "EffectsBrowser.h"
#include "../captions/OverlayRenderer.h"
#include "../core/TimelineOps.h"
#include "../media/ColorMath.h"
#include "../media/ImageFx.h"

#include <QLineEdit>
#include <QResizeEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace ss {

static const char *kEffectMime = "application/x-shyftr-effect";

// ------------------------------------------------------------------ library content

namespace Library {

QVector<TextTemplate> textTemplates()
{
    auto T = [](QString id, QString label, std::function<void(TitleProps &)> fn) {
        TitleProps p;
        p.animation = "fade";
        fn(p);
        return TextTemplate{id, label, p};
    };
    return {
        T("bold-pop", "Bold pop", [](TitleProps &p) {
            p.text = "BIG NEWS";
            p.fontFamily = "Arial Black";
            p.weight = 900;
            p.sizePct = 0.11;
            p.outlineColor = Qt::black;
            p.outlineWidthPct = 0.1;
            p.animation = "pop";
            p.loop = "pulse";
        }),
        T("neon", "Neon sign", [](TitleProps &p) {
            p.text = "NEON NIGHTS";
            p.fontFamily = "Futura";
            p.weight = 800;
            p.sizePct = 0.1;
            p.color = QColor("#FF5FD2");
            p.gradientColor = QColor("#7B5CFF");
            p.outlineColor = QColor("#12062A");
            p.outlineWidthPct = 0.05;
            p.loop = "glow";
        }),
        T("gradient", "Gradient headline", [](TitleProps &p) {
            p.text = "GAME CHANGER";
            p.fontFamily = "Impact";
            p.weight = 900;
            p.sizePct = 0.12;
            p.color = QColor("#FFE259");
            p.gradientColor = QColor("#FF7A1A");
            p.outlineColor = Qt::black;
            p.outlineWidthPct = 0.09;
            p.shadowColor = QColor(0, 0, 0, 170);
            p.animation = "zoom";
        }),
        T("lower-clean", "Lower third", [](TitleProps &p) {
            p.text = "Your Name\nTitle · Company";
            p.sizePct = 0.045;
            p.x = 0.27;
            p.y = 0.82;
            p.align = "left";
            p.backgroundColor = QColor(0, 0, 0, 170);
            p.animation = "slideRight";
            p.animationOut = "slideLeft";
        }),
        T("breaking", "Breaking news", [](TitleProps &p) {
            p.text = "BREAKING NEWS";
            p.weight = 900;
            p.sizePct = 0.05;
            p.y = 0.84;
            p.backgroundColor = QColor("#E63946");
            p.boxRadiusPct = 0.05;
            p.letterSpacingPct = 6;
            p.animation = "slideUp";
        }),
        T("subscribe", "Subscribe button", [](TitleProps &p) {
            p.text = "SUBSCRIBE";
            p.weight = 900;
            p.sizePct = 0.06;
            p.y = 0.8;
            p.backgroundColor = QColor("#FF0000");
            p.boxPaddingPct = 0.6;
            p.boxRadiusPct = 0.35;
            p.animation = "bounce";
            p.loop = "pulse";
        }),
        T("follow", "Follow call-out", [](TitleProps &p) {
            p.text = "Follow for more →";
            p.sizePct = 0.06;
            p.y = 0.75;
            p.color = QColor("#111111");
            p.backgroundColor = QColor("#FFD43B");
            p.boxRadiusPct = 0.3;
            p.animation = "pop";
            p.loop = "float";
        }),
        T("quote", "Quote", [](TitleProps &p) {
            p.text = "“Make it simple,\nbut significant.”";
            p.fontFamily = "Georgia";
            p.weight = 600;
            p.italic = true;
            p.sizePct = 0.065;
            p.shadowColor = QColor(0, 0, 0, 150);
        }),
        T("chapter", "Chapter title", [](TitleProps &p) {
            p.text = "CHAPTER ONE";
            p.weight = 300;
            p.sizePct = 0.05;
            p.letterSpacingPct = 40;
            p.animation = "blur";
        }),
        T("price", "Price tag", [](TitleProps &p) {
            p.text = "₹999 ONLY";
            p.weight = 900;
            p.sizePct = 0.07;
            p.color = QColor("#111111");
            p.backgroundColor = QColor("#FFD43B");
            p.boxRadiusPct = 0.12;
            p.animation = "drop";
            p.loop = "wiggle";
        }),
        T("handwritten", "Handwritten", [](TitleProps &p) {
            p.text = "made with love";
            p.fontFamily = "Marker Felt";
            p.weight = 500;
            p.sizePct = 0.08;
            p.color = QColor("#FFF3B0");
            p.animation = "typewriter";
        }),
        T("terminal", "Typewriter", [](TitleProps &p) {
            p.text = "> loading ideas...";
            p.fontFamily = "Courier New";
            p.weight = 700;
            p.sizePct = 0.05;
            p.color = QColor("#9EF01A");
            p.backgroundColor = QColor(0, 0, 0, 200);
            p.boxRadiusPct = 0.05;
            p.animation = "typewriter";
        }),
        T("retro", "Retro 3D", [](TitleProps &p) {
            p.text = "RETRO";
            p.fontFamily = "Arial Black";
            p.weight = 900;
            p.sizePct = 0.14;
            p.shadowColor = QColor("#5B2BD6");
            p.shadowOffsetPct = 0.1;
            p.animation = "pop";
        }),
        T("bubble", "Speech bubble", [](TitleProps &p) {
            p.text = "Hello there!";
            p.weight = 800;
            p.sizePct = 0.06;
            p.color = QColor("#16161A");
            p.backgroundColor = Qt::white;
            p.boxPaddingPct = 0.6;
            p.boxRadiusPct = 0.6;
            p.animation = "pop";
            p.loop = "wiggle";
        }),
        T("minimal", "Minimal", [](TitleProps &p) {
            p.text = "minimal and clean";
            p.fontFamily = "Helvetica Neue";
            p.weight = 400;
            p.sizePct = 0.045;
            p.letterSpacingPct = 4;
        }),
        T("countdown", "Countdown number", [](TitleProps &p) {
            p.text = "3";
            p.weight = 900;
            p.sizePct = 0.3;
            p.outlineColor = Qt::black;
            p.outlineWidthPct = 0.06;
            p.animation = "pop";
            p.animationOut = "zoom";
        }),
        T("engage", "Like · comment · share", [](TitleProps &p) {
            p.text = "👍 Like   💬 Comment   🔁 Share";
            p.sizePct = 0.045;
            p.y = 0.86;
            p.backgroundColor = QColor(0, 0, 0, 150);
            p.boxRadiusPct = 0.5;
            p.animation = "slideUp";
        }),
        T("headline-bar", "Headline bar", [](TitleProps &p) {
            p.text = "TOP 5 TIPS";
            p.weight = 900;
            p.sizePct = 0.07;
            p.y = 0.15;
            p.backgroundColor = QColor("#5B8CFF");
            p.boxRadiusPct = 0.2;
            p.animation = "zoom";
            p.loop = "float";
        }),
    };
}

QStringList stickers()
{
    return {"🔥", "😂", "❤️", "👍", "👏", "🎉", "✨", "💯", "😍", "🤯", "😎", "👀", "✅", "❌", "⭐", "💥",
            "🚀", "📌", "👉", "👇", "🎵", "💡", "⚡", "🙏", "😱", "🤔", "💰", "🏆", "📈", "🎁"};
}

} // namespace Library

// ------------------------------------------------------------------ thumbnails

namespace {

QImage sampleImage(QSize s, int variant)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, s.height());
    if (variant == 0) {
        sky.setColorAt(0, QColor("#2E5BBA"));
        sky.setColorAt(0.6, QColor("#F39C6B"));
        sky.setColorAt(1, QColor("#FFD59E"));
    } else {
        sky.setColorAt(0, QColor("#0F3D3E"));
        sky.setColorAt(0.6, QColor("#2A9D8F"));
        sky.setColorAt(1, QColor("#E9C46A"));
    }
    p.fillRect(img.rect(), sky);
    p.setPen(Qt::NoPen);
    p.setBrush(variant == 0 ? QColor("#FFE8A3") : QColor("#F1FAEE"));
    p.drawEllipse(QPointF(s.width() * (variant == 0 ? 0.7 : 0.28), s.height() * 0.42), s.height() * 0.16, s.height() * 0.16);
    QPainterPath hills;
    hills.moveTo(0, s.height());
    hills.lineTo(0, s.height() * 0.72);
    hills.cubicTo(s.width() * 0.3, s.height() * 0.55, s.width() * 0.55, s.height() * 0.9, s.width(), s.height() * 0.66);
    hills.lineTo(s.width(), s.height());
    p.setBrush(variant == 0 ? QColor("#3D2C5C") : QColor("#264653"));
    p.drawPath(hills);
    return img;
}

QImage darkTile(QSize s)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    QLinearGradient g(0, 0, s.width(), s.height());
    g.setColorAt(0, QColor("#2b3446"));
    g.setColorAt(1, QColor("#3d2f4f"));
    p.fillRect(img.rect(), g);
    return img;
}

QImage textThumb(QSize s, const TitleProps &tp)
{
    QImage img = darkTile(s);
    Clip c;
    c.type = ClipType::Title;
    c.title = tp;
    c.title.y = std::clamp(tp.y, 0.3, 0.7);
    c.title.x = 0.5;
    c.title.align = "center";
    c.title.sizePct = std::min(tp.sizePct * 1.6, 0.34);
    c.start = 0;
    c.in = 0;
    c.out = 4 * kUsPerSec;
    QPainter p(&img);
    GraphicsRenderer::paintTitle(p, s, c, 2 * kUsPerSec);
    return img;
}

QImage glyphTile(QSize s, const QString &glyph, const QColor &bg, double scale = 0.55)
{
    QImage img(s, QImage::Format_ARGB32_Premultiplied);
    img.fill(bg);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f;
    f.setFamilies({"Apple Color Emoji", "Segoe UI Emoji", "Noto Color Emoji", "Segoe UI Symbol", "DejaVu Sans"});
    f.setPixelSize(int(s.height() * scale));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(img.rect(), Qt::AlignCenter, glyph);
    return img;
}

QImage curveThumb(QSize s, const SpeedCurve &c)
{
    QImage img = darkTile(s);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    double peak = 0;
    for (const QPointF &pt : c.points)
        peak = std::max(peak, pt.y());
    QPainterPath path;
    for (int i = 0; i < c.points.size(); ++i) {
        const QPointF pt(8 + c.points[i].x() * (s.width() - 16), s.height() - 10 - c.points[i].y() / std::max(1.0, peak) * (s.height() - 22));
        i ? path.lineTo(pt) : path.moveTo(pt);
    }
    p.setPen(QPen(QColor("#FFD43B"), 2.5));
    p.drawPath(path);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DashLine));
    p.drawLine(8, s.height() - 10, s.width() - 8, s.height() - 10);
    return img;
}

QListWidgetItem *makeItem(const QImage &thumb, const QString &label, const QString &id, const QString &tip)
{
    auto *it = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)), label);
    it->setData(Qt::UserRole, id);
    it->setToolTip(tip.isEmpty() ? label : tip);
    return it;
}

} // namespace

// ------------------------------------------------------------------ LibraryList

LibraryList::LibraryList(QWidget *parent) : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setSpacing(4);
    setWordWrap(true);
    setUniformItemSizes(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void LibraryList::setBaseThumb(QSize s)
{
    m_base = s;
    fitGrid();
}

void LibraryList::resizeEvent(QResizeEvent *e)
{
    QListWidget::resizeEvent(e);
    fitGrid();
}

void LibraryList::fitGrid()
{
    // always at least two columns that fill the panel width
    const int vw = std::max(80, viewport()->width() - 4);
    const int cols = std::max(2, vw / (m_base.width() + 12));
    const int cell = vw / cols;
    const int iw = std::max(24, cell - 12);
    const int ih = std::max(16, iw * m_base.height() / std::max(1, m_base.width()));
    setIconSize(QSize(iw, ih));
    setGridSize(QSize(cell, ih + 32));
}

QStringList LibraryList::mimeTypes() const
{
    return {kEffectMime};
}

QMimeData *LibraryList::mimeData(const QList<QListWidgetItem *> &items) const
{
    if (items.isEmpty())
        return nullptr;
    const QString id = items.first()->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return nullptr;
    auto *md = new QMimeData;
    md->setData(kEffectMime, id.toUtf8());
    md->setText(items.first()->text());
    return md;
}

// ------------------------------------------------------------------ browser

EffectsBrowser::EffectsBrowser(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    setAccessibleName("Library");
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search text, stickers, effects, transitions, filters…");
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    v->addWidget(m_tabs, 1);
    connect(m_search, &QLineEdit::textChanged, this, &EffectsBrowser::filter);
    build();
}

LibraryList *EffectsBrowser::addPage(const QString &title, int w, int h)
{
    auto *list = new LibraryList;
    list->setBaseThumb(QSize(w, h));
    m_tabs->addTab(list, title);
    m_lists << list;
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            applyEffect(id);
    });
    return list;
}

void EffectsBrowser::showCategory(const QString &name)
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i) == name)
            m_tabs->setCurrentIndex(i);
}

void EffectsBrowser::filter(const QString &text)
{
    const QString q = text.trimmed();
    int firstHit = -1;
    for (int li = 0; li < m_lists.size(); ++li) {
        LibraryList *l = m_lists[li];
        int visible = 0;
        for (int i = 0; i < l->count(); ++i) {
            QListWidgetItem *it = l->item(i);
            const bool show = q.isEmpty() || it->text().contains(q, Qt::CaseInsensitive) || it->toolTip().contains(q, Qt::CaseInsensitive);
            it->setHidden(!show);
            visible += show;
        }
        if (visible && firstHit < 0)
            firstHit = li;
    }
    if (!q.isEmpty() && firstHit >= 0 && m_lists[m_tabs->currentIndex()]->findItems(q, Qt::MatchContains).isEmpty())
        m_tabs->setCurrentIndex(firstHit);
}

void EffectsBrowser::build()
{
    const QSize wide(88, 50);   // two columns fit a ~230 px panel
    const QSize square(52, 52);
    const QImage a = sampleImage(wide, 0), b = sampleImage(wide, 1);

    // Text templates
    LibraryList *text = addPage("Text", wide.width(), wide.height());
    for (const auto &t : Library::textTemplates())
        text->addItem(makeItem(textThumb(wide, t.props), t.label, "text:" + t.id,
                               t.label + "\nDouble-click to add at the playhead, or drag onto the video"));

    // Stickers
    LibraryList *st = addPage("Stickers", square.width(), square.height());
    for (const QString &e : Library::stickers())
        st->addItem(makeItem(glyphTile(square, e, QColor("#2b3446")), QString(), "sticker:" + e, "Sticker " + e));

    // Effects
    LibraryList *fx = addPage("Effects", wide.width(), wide.height());
    for (const auto &e : ImageFx::effects()) {
        QImage img = a.copy();
        EffectSpec spec{e.id, 0.85, 1.0};
        if (ImageFx::isMotionEffect(e.id)) {
            double dx = 0, dy = 0, sc = 1, rot = 0;
            ImageFx::effectMotion(spec, 0.13, 0.9, &dx, &dy, &sc, &rot);
            QImage moved(img.size(), QImage::Format_ARGB32_Premultiplied);
            moved.fill(Qt::black);
            QPainter q(&moved);
            q.setRenderHint(QPainter::SmoothPixmapTransform);
            q.translate(img.width() / 2.0 + dx * img.width(), img.height() / 2.0 + dy * img.height());
            q.rotate(rot + (e.id == "shake" ? 4 : 0));
            q.scale(sc * 1.1, sc * 1.1);
            q.translate(-img.width() / 2.0, -img.height() / 2.0);
            q.drawImage(0, 0, img);
            q.end();
            img = moved;
        } else {
            ImageFx::applyEffect(img, spec, e.id == "flash" ? 0.02 : 0.37, 0.5, 7);
        }
        fx->addItem(makeItem(img, e.label, "fx:" + e.id,
                             QString("%1 · %2\nDouble-click: apply to the selected clips (or add an effect layer)\n"
                                     "Drag onto a clip, or onto empty timeline space for an effect layer")
                                 .arg(e.group, e.label)));
    }

    // Transitions
    LibraryList *tr = addPage("Transitions", wide.width(), wide.height());
    for (const auto &t : ImageFx::transitions()) {
        QImage img(wide, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter q(&img);
        ImageFx::drawTransition(q, a, b, t.id, t.id == "spin" ? 0.3 : 0.45, 3);
        q.end();
        tr->addItem(makeItem(img, t.label, "tr:" + t.id,
                             t.label + "\nDouble-click: add to the selected clip's start\nDrag onto a cut between two clips"));
    }

    // Filters
    LibraryList *fl = addPage("Filters", wide.width(), wide.height());
    for (const auto &l : ImageFx::looks()) {
        QImage img = a.convertToFormat(QImage::Format_ARGB32);
        ColorMath::apply(img, l.color);
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        ImageFx::colorAdjust(img, l.adjust, 1);
        fl->addItem(makeItem(img, l.label, "look:" + l.id,
                             l.label + "\nDouble-click: apply to the selected clips (or add a filter layer)"));
    }

    // Shapes
    LibraryList *sh = addPage("Shapes", square.width(), square.height());
    for (const auto &[label, id] : QVector<QPair<QString, QString>>{{"Arrow", "shape-arrow"}, {"Highlight box", "shape-highlight"},
                                                                     {"Circle", "shape-circle"}, {"Star", "shape-star"},
                                                                     {"Pill tag", "shape-pill"}, {"Triangle", "shape-triangle"},
                                                                     {"Lower bar", "shape-bar"}, {"Divider line", "shape-line"},
                                                                     {"Frame", "shape-box"}}) {
        static const QHash<QString, QString> glyphs{{"shape-arrow", "➜"}, {"shape-highlight", "▢"}, {"shape-circle", "●"},
                                                    {"shape-star", "★"}, {"shape-pill", "⬬"}, {"shape-triangle", "▲"},
                                                    {"shape-bar", "▬"}, {"shape-line", "―"}, {"shape-box", "⬚"}};
        sh->addItem(makeItem(glyphTile(square, glyphs.value(id), QColor("#2b3446"), 0.5), label, id, label + "\nDrag onto the video to place it"));
    }

    // Speed curves
    LibraryList *sp = addPage("Speed", wide.width(), wide.height());
    static const QHash<QString, QString> curveNames{{"montage", "Montage"}, {"hero", "Hero"}, {"bullet", "Bullet time"},
                                                    {"jumpCut", "Jump cut"}, {"flashIn", "Flash in"}, {"flashOut", "Flash out"},
                                                    {"rampUp", "Ramp up"}, {"rampDown", "Ramp down"}};
    for (const QString &c : SpeedCurve::presetNames())
        sp->addItem(makeItem(curveThumb(wide, SpeedCurve::fromPreset(c)), curveNames.value(c, c), "curve:" + c,
                             curveNames.value(c, c) + " speed curve\nApply to the selected video clips"));
    for (const auto &[label, id] : QVector<QPair<QString, QString>>{{"0.5× slow", "speed-0.5"}, {"1× normal", "speed-1"},
                                                                     {"1.5× fast", "speed-1.5"}, {"2× fast", "speed-2"},
                                                                     {"No curve", "curve:"}})
        sp->addItem(makeItem(glyphTile(wide, label.section(' ', 0, 0), QColor("#34405a"), 0.4), label, id, label));

    // Voice & audio
    LibraryList *au = addPage("Audio", square.width(), square.height());
    static const QHash<QString, QString> voiceGlyph{{"chipmunk", "🐿"}, {"deep", "🎙"}, {"robot", "🤖"}, {"echo", "🔁"},
                                                    {"hall", "⛪"}, {"telephone", "☎"}, {"megaphone", "📣"}, {"radio", "📻"},
                                                    {"underwater", "🌊"}, {"monster", "👹"}};
    static const QHash<QString, QString> voiceLabel{{"chipmunk", "Chipmunk"}, {"deep", "Deep voice"}, {"robot", "Robot"},
                                                    {"echo", "Echo"}, {"hall", "Hall reverb"}, {"telephone", "Telephone"},
                                                    {"megaphone", "Megaphone"}, {"radio", "Radio"}, {"underwater", "Underwater"},
                                                    {"monster", "Monster"}};
    for (const QString &v : AudioFx::voiceNames())
        au->addItem(makeItem(glyphTile(square, voiceGlyph.value(v), QColor("#2f3b4f")), voiceLabel.value(v, v), "voice:" + v,
                             voiceLabel.value(v, v) + " voice effect\nApply to the selected audio (or the sound of selected video)"));
    for (const auto &[label, id, glyph] : QVector<std::tuple<QString, QString, QString>>{
             {"Reduce noise", "voice:denoise", "🔇"}, {"Enhance voice", "voice:enhance", "✨"}, {"Normal voice", "voice:", "🗣"},
             {"Fade in/out", "audio-fade", "〰"}, {"−6 dB", "audio-minus6", "🔉"}, {"Music −14 dB", "audio-duck", "🎵"},
             {"Mute", "audio-mute", "🔈"}})
        au->addItem(makeItem(glyphTile(square, glyph, QColor("#2f3b4f")), label, id, label));

    // Basics (fades, framing)
    LibraryList *bs = addPage("Basics", square.width(), square.height());
    for (const auto &[label, id, glyph] : QVector<std::tuple<QString, QString, QString>>{
             {"Fade in", "fade-in", "◐"}, {"Fade out", "fade-out", "◑"}, {"Fade in + out", "fade-both", "◒"},
             {"Dip to black", "dip-black", "■"}, {"Remove fades", "fade-none", "□"}, {"Fill frame", "frame-fill", "⛶"},
             {"Fit frame", "frame-fit", "▣"}, {"Punch-in 120%", "frame-punch", "🔍"}, {"Rotate 90°", "frame-rotate", "⟳"},
             {"Reset colour", "look-reset", "↺"}})
        bs->addItem(makeItem(glyphTile(square, glyph, QColor("#34405a"), 0.45), label, id, label + "\nApplies to the selected clips"));
}

// ------------------------------------------------------------------ apply

QString EffectsBrowser::dropKind(const QString &id)
{
    if (id.startsWith("text:") || id.startsWith("sticker:") || id.startsWith("title-") || id.startsWith("shape-"))
        return "clip";
    if (id.startsWith("fx:") || id.startsWith("look:"))
        return "either";
    return "target";
}

TimeUs EffectsBrowser::dropDuration(const QString &id)
{
    if (id.startsWith("sticker:") || id.startsWith("fx:") || id.startsWith("look:"))
        return 3 * kUsPerSec;
    return 4 * kUsPerSec;
}

void EffectsBrowser::applyEffect(const QString &id, const QPointF &pos, const QString &targetClip, TimeUs atIn,
                                 const QString &trackId)
{
    if (!m_doc->isOpen())
        return;
    m_targetTrack = trackId;
    struct ResetTrack {
        QString &t;
        ~ResetTrack() { t.clear(); }
    } resetTrack{m_targetTrack};
    const QStringList sel = targetClip == "-" ? QStringList()
                            : targetClip.isEmpty() ? m_doc->selectedClips() : QStringList{targetClip};
    const TimeUs at = atIn >= 0 ? atIn : m_doc->playhead();
    const bool placed = pos.x() >= 0 && pos.y() >= 0;
    const Project &proj = m_doc->project();
    auto isVisual = [&](const QString &cid) {
        const Track *t = nullptr;
        const Clip *c = proj.findClip(cid, &t);
        return c && t && t->kind == TrackKind::Video;
    };
    QStringList visual;
    for (const QString &cid : sel)
        if (isVisual(cid))
            visual << cid;

    auto forSel = [&](const QString &what, const std::function<void(Clip &, Track &)> &fn) {
        if (sel.isEmpty()) {
            emit statusMessage(targetClip == "-" ? "Drop this onto a clip on the timeline"
                                                 : "Select one or more clips first");
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
    auto forClips = [&](const QStringList &ids, const QString &what, const std::function<void(Clip &, Track &)> &fn) {
        return m_doc->apply(what, [&](Project &p) {
            bool any = false;
            for (const QString &cid : ids) {
                Track *t = nullptr;
                Clip *c = p.findClip(cid, &t);
                if (c && !t->locked) {
                    fn(*c, *t);
                    any = true;
                }
            }
            return any;
        });
    };
    auto look = [&](double ex, double co, double sa, double te, double ti) {
        forSel("Colour look", [=](Clip &c, Track &) {
            c.color.exposure = ex;
            c.color.contrast = co;
            c.color.saturation = sa;
            c.color.temperature = te;
            c.color.tint = ti;
            c.fx.look.clear();
        });
    };
    const TimeUs half = 500 * kUsPerMs;

    // ---- new library items
    if (id.startsWith("text:")) {
        for (const auto &t : Library::textTemplates())
            if ("text:" + t.id == id) {
                TitleProps tp = t.props;
                if (placed) {
                    tp.x = tp.align == "left" ? std::clamp(pos.x() + 0.12, 0.0, 1.0) : pos.x();
                    tp.y = pos.y();
                }
                QString newId;
                m_doc->apply("Add text", [&](Project &p) {
                    newId = TimelineOps::addTitleClip(p, m_targetTrack, at, 4 * kUsPerSec, tp);
                    return !newId.isEmpty();
                });
                if (!newId.isEmpty())
                    m_doc->setSelectedClips({newId});
                emit statusMessage("Text added — double-click it on the video to type your own words");
            }
        return;
    }
    if (id.startsWith("sticker:")) {
        TitleProps tp;
        tp.text = id.mid(8);
        tp.sizePct = 0.16;
        tp.weight = 400;
        tp.animation = "pop";
        tp.loop = "float";
        if (placed) {
            tp.x = pos.x();
            tp.y = pos.y();
        }
        QString newId;
        m_doc->apply("Add sticker", [&](Project &p) {
            newId = TimelineOps::addTitleClip(p, m_targetTrack, at, 3 * kUsPerSec, tp);
            if (Clip *c = p.findClip(newId))
                c->name = "Sticker " + tp.text;
            return !newId.isEmpty();
        });
        if (!newId.isEmpty())
            m_doc->setSelectedClips({newId});
        return;
    }
    if (id.startsWith("fx:")) {
        const QString type = id.mid(3);
        if (!visual.isEmpty()) {
            forClips(visual, "Add effect", [type](Clip &c, Track &) { c.fx.effects << EffectSpec{type, 0.6, 1.0}; });
            emit statusMessage(ImageFx::effectLabel(type) + " added — adjust it in Inspector → Effects");
        } else {
            ClipFx fx;
            fx.effects << EffectSpec{type, 0.6, 1.0};
            QString newId;
            m_doc->apply("Add effect layer", [&](Project &p) {
                newId = TimelineOps::addEffectClip(p, m_targetTrack, at, 3 * kUsPerSec, fx, ImageFx::effectLabel(type));
                return !newId.isEmpty();
            });
            if (!newId.isEmpty())
                m_doc->setSelectedClips({newId});
            emit statusMessage("Effect layer added: it affects everything on the tracks below it. Trim it on the timeline.");
        }
        return;
    }
    if (id.startsWith("tr:")) {
        const QString type = id.mid(3);
        QStringList targets = visual;
        if (targets.isEmpty()) {
            // the cut nearest the playhead on the top-most video track that has one
            TimeUs best = std::numeric_limits<TimeUs>::max();
            for (const Track &t : proj.tracks)
                if (t.kind == TrackKind::Video)
                    for (int i = 1; i < t.clips.size(); ++i)
                        if (std::llabs(t.clips[i - 1].end() - t.clips[i].start) <= frameDurationUs(proj.canvas.fps)
                            && std::llabs(t.clips[i].start - at) < best) {
                            best = std::llabs(t.clips[i].start - at);
                            targets = {t.clips[i].id};
                        }
        }
        if (targets.isEmpty()) {
            emit statusMessage("Put two clips next to each other on a track, then add a transition at the cut");
            return;
        }
        int withoutPrev = 0;
        forClips(targets, "Add transition", [&](Clip &c, Track &t) {
            const int i = t.indexOfClip(c.id);
            if (i <= 0 || std::llabs(t.clips[i - 1].end() - c.start) > frameDurationUs(proj.canvas.fps))
                ++withoutPrev;
            c.fx.transitionIn = {type, c.fx.transitionIn.isSet() ? c.fx.transitionIn.duration : 500 * kUsPerMs};
        });
        emit statusMessage(withoutPrev ? "Transition set — it plays when another clip sits right before this one"
                                       : ImageFx::transitionLabel(type) + " transition added");
        return;
    }
    if (id.startsWith("look:")) {
        const QString lid = id.mid(5);
        for (const auto &l : ImageFx::looks())
            if (l.id == lid) {
                if (!visual.isEmpty()) {
                    forClips(visual, "Filter", [l](Clip &c, Track &) {
                        c.color = l.color;
                        const QString lut = c.fx.adjust.lut;
                        const double ls = c.fx.adjust.lutStrength;
                        c.fx.adjust = l.adjust;
                        c.fx.adjust.lut = lut;
                        c.fx.adjust.lutStrength = ls;
                        c.fx.look = l.id == "none" ? QString() : l.id;
                    });
                } else {
                    ClipFx fx;
                    fx.adjust = l.adjust;
                    fx.look = l.id;
                    QString newId;
                    m_doc->apply("Add filter layer", [&](Project &p) {
                        newId = TimelineOps::addEffectClip(p, m_targetTrack, at, 3 * kUsPerSec, fx, "Filter: " + l.label);
                        if (Clip *c = p.findClip(newId))
                            c->color = l.color;
                        return !newId.isEmpty();
                    });
                    if (!newId.isEmpty())
                        m_doc->setSelectedClips({newId});
                    emit statusMessage("Filter layer added: it colours everything on the tracks below it");
                }
            }
        return;
    }
    if (id.startsWith("curve:")) {
        const QString c = id.mid(6);
        QStringList media;
        for (const QString &cid : visual)
            if (const Clip *cl = proj.findClip(cid); cl && cl->type == ClipType::Media)
                media << cid;
        if (media.isEmpty())
            return emit statusMessage("Select a video clip first");
        forClips(media, "Speed curve", [c](Clip &cl, Track &) { cl.fx.speedCurve = c.isEmpty() ? SpeedCurve() : SpeedCurve::fromPreset(c); });
        emit statusMessage(c.isEmpty() ? QString("Speed curve removed") : QString("Speed curve applied (the clip's sound is muted while it is on)"));
        return;
    }
    if (id.startsWith("voice:")) {
        const QString v = id.mid(6);
        QStringList audio;
        for (const QString &cid : sel) {
            const Track *t = nullptr;
            const Clip *c = proj.findClip(cid, &t);
            if (!c)
                continue;
            if (t->kind == TrackKind::Audio)
                audio << cid;
            else
                for (const Clip *l : const_cast<Project &>(proj).linkedClips(cid)) {
                    const Track *lt = nullptr;
                    proj.findClip(l->id, &lt);
                    if (lt && lt->kind == TrackKind::Audio)
                        audio << l->id;
                }
        }
        if (audio.isEmpty())
            return emit statusMessage("Select an audio clip (or a video clip with sound) first");
        forClips(audio, "Voice effect", [v](Clip &c, Track &) {
            if (v == "denoise")
                c.fx.audio.denoise = !c.fx.audio.denoise;
            else if (v == "enhance")
                c.fx.audio.enhance = !c.fx.audio.enhance;
            else
                c.fx.audio.voice = v;
        });
        emit statusMessage("Voice effect applied — the preview switches to it after a short render");
        return;
    }

    // ---- classic items
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
            tp.x = tp.align == "left" ? std::clamp(pos.x() + 0.12, 0.0, 1.0) : pos.x();
            tp.y = pos.y();
        }
        m_doc->apply("Add title", [&](Project &p) { return !TimelineOps::addTitleClip(p, m_targetTrack, at, 4 * kUsPerSec, tp).isEmpty(); });
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
        if (id == "shape-circle" || id == "shape-star") { // round on any aspect ratio
            const auto &cv = proj.canvas;
            sp.h = sp.w * cv.width / std::max(1, cv.height);
        }
        if (placed) {
            if (sp.w < 1.0)
                sp.x = pos.x();
            sp.y = pos.y();
        }
        m_doc->apply("Add shape", [&](Project &p) { return !TimelineOps::addShapeClip(p, m_targetTrack, at, 4 * kUsPerSec, sp).isEmpty(); });
    } else if (id == "look-reset") {
        look(0, 1, 1, 0, 0);
        forSel("Reset filter", [](Clip &c, Track &) { c.fx.adjust = ExtraAdjust(); });
    } else if (id == "frame-fill")
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
