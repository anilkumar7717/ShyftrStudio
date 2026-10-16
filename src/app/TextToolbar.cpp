#include "TextToolbar.h"
#include "../captions/TextStyles.h"
#include "TextStyleUi.h"
#include "Widgets.h"

#include <QFontComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>
#include <QWidgetAction>

namespace ss {

TextToolbar::TextToolbar(Document *doc, QWidget *parent) : QFrame(parent), m_doc(doc)
{
    setObjectName("textToolbar");
    setStyleSheet("#textToolbar { background: rgba(22,24,30,235); border: 1px solid #3A3F4B; border-radius: 8px; }"
                  "QToolButton { min-width: 26px; padding: 2px 4px; }"
                  "QToolButton:checked { background: #5B8CFF; color: white; border-radius: 4px; }");
    setAccessibleName("Text formatting");
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(6, 4, 6, 4);
    h->setSpacing(4);

    m_font = new QFontComboBox;
    m_font->setToolTip("Font");
    m_font->setMaximumWidth(170);
    m_font->setMinimumContentsLength(8);
    m_size = new QSpinBox;
    m_size->setRange(8, 400);
    m_size->setToolTip("Size (% of a tenth of the video height)");
    m_size->setAccelerated(true);
    auto tb = [](const QString &text, const QString &tip, bool checkable) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setAutoRaise(true);
        return b;
    };
    m_bold = tb("B", "Bold", true);
    QFont bf = m_bold->font();
    bf.setBold(true);
    m_bold->setFont(bf);
    m_italic = tb("I", "Italic", true);
    QFont itf = m_italic->font();
    itf.setItalic(true);
    m_italic->setFont(itf);
    m_underline = tb("U", "Underline", true);
    QFont uf = m_underline->font();
    uf.setUnderline(true);
    m_underline->setFont(uf);
    m_case = tb("Aa", "Letter case", false);
    m_case->setPopupMode(QToolButton::InstantPopup);
    auto *caseMenu = new QMenu(m_case);
    for (const auto &opt : QVector<QPair<QString, QString>>{{"As typed", ""}, {"UPPERCASE", "upper"}, {"lowercase", "lower"}, {"Title Case", "title"}}) {
        const QString v = opt.second;
        caseMenu->addAction(opt.first, this, [this, v]() { change("Letter case", "case", [v](TitleProps &t) { t.textCase = v; }); });
    }
    m_case->setMenu(caseMenu);
    m_align = tb("≡", "Alignment", false);
    m_align->setPopupMode(QToolButton::InstantPopup);
    auto *alignMenu = new QMenu(m_align);
    for (const auto &opt : QVector<QPair<QString, QString>>{{"Align left", "left"}, {"Align centre", "center"}, {"Align right", "right"}}) {
        const QString v = opt.second;
        alignMenu->addAction(opt.first, this, [this, v]() { change("Align text", "align", [v](TitleProps &t) { t.align = v; }); });
    }
    m_align->setMenu(alignMenu);
    m_color = new ColorButton;
    m_color->setToolTip("Text colour");
    m_color->setFixedWidth(34);
    m_highlight = new ColorButton;
    m_highlight->setToolTip("Highlight colour for *words between stars*");
    m_highlight->setFixedWidth(34);
    m_styles = tb("Styles ▾", "Professional text styles", false);
    m_styles->setPopupMode(QToolButton::InstantPopup);
    auto *stylesMenu = new QMenu(m_styles);
    auto *wa = new QWidgetAction(stylesMenu);
    auto *grid = TextStyleUi::makeGrid(nullptr, QSize(84, 47));
    grid->setFixedSize(4 * (84 + 10) + 24, 3 * (47 + 26) + 12);
    wa->setDefaultWidget(grid);
    stylesMenu->addAction(wa);
    connect(grid, &QListWidget::itemClicked, this, [this, stylesMenu](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        change("Text style: " + it->text(), "style", [id](TitleProps &t) { TextStyles::apply(t, id); });
        stylesMenu->close();
    });
    m_styles->setMenu(stylesMenu);
    auto *editBtn = tb("✎", "Edit the words (or double-click the text)", false);

    for (QWidget *w : std::initializer_list<QWidget *>{m_styles, m_font, m_size, m_bold, m_italic, m_underline, m_case, m_align,
                                                       m_color, m_highlight, editBtn})
        h->addWidget(w);

    connect(m_font, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        const QString fam = f.family();
        change("Font", "font", [fam](TitleProps &t) { t.fontFamily = fam; });
    });
    connect(m_size, &QSpinBox::valueChanged, this, [this](int v) {
        change("Text size", "size", [v](TitleProps &t) { t.sizePct = v / 1000.0; });
    });
    connect(m_bold, &QToolButton::toggled, this, [this](bool on) {
        change("Bold", "bold", [on](TitleProps &t) { t.weight = on ? std::max(700, t.weight >= 700 ? t.weight : 700) : 400; });
    });
    connect(m_italic, &QToolButton::toggled, this, [this](bool on) { change("Italic", "italic", [on](TitleProps &t) { t.italic = on; }); });
    connect(m_underline, &QToolButton::toggled, this, [this](bool on) { change("Underline", "underline", [on](TitleProps &t) { t.underline = on; }); });
    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) { change("Text colour", "color", [c](TitleProps &t) { t.color = c; }); });
    connect(m_highlight, &ColorButton::colorChanged, this, [this](const QColor &c) { change("Highlight colour", "hl", [c](TitleProps &t) { t.highlightColor = c; }); });
    connect(editBtn, &QToolButton::clicked, this, [this]() { emit editTextRequested(m_clip); });
    connect(doc, &Document::projectChanged, this, &TextToolbar::refresh);
    hide();
}

void TextToolbar::setClip(const QString &clipId)
{
    m_clip = clipId;
    refresh();
}

void TextToolbar::refresh()
{
    const Clip *c = m_clip.isEmpty() ? nullptr : m_doc->project().findClip(m_clip);
    if (!c || c->type != ClipType::Title) {
        hide();
        return;
    }
    m_updating = true;
    const TitleProps &t = c->title;
    m_font->setCurrentFont(QFont(TextStyleUi::installedFamily(t.fontFamily)));
    m_size->setValue(int(std::lround(t.sizePct * 1000)));
    m_bold->setChecked(t.weight >= 600);
    m_italic->setChecked(t.italic);
    m_underline->setChecked(t.underline);
    m_color->setColor(t.color);
    m_highlight->setColor(t.highlightColor);
    m_updating = false;
    adjustSize();
}

void TextToolbar::change(const QString &what, const QString &key, const std::function<void(TitleProps &)> &fn)
{
    if (m_updating || m_clip.isEmpty())
        return;
    const QString id = m_clip;
    // only this text; successive changes of one control merge into one undo step
    m_doc->apply(what, [&](Project &p) {
        Track *tr = nullptr;
        Clip *c = p.findClip(id, &tr);
        if (!c || c->type != ClipType::Title || tr->locked)
            return false;
        fn(c->title);
        return true;
    }, "texttb:" + key + ":" + id);
}

} // namespace ss
