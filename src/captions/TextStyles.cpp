#include "TextStyles.h"

namespace ss {
namespace TextStyles {

static void resetLook(TitleProps &t)
{
    const TitleProps d;
    t.fontFamily = d.fontFamily;
    t.weight = d.weight;
    t.italic = false;
    t.color = d.color;
    t.outlineColor = d.outlineColor;
    t.outlineWidthPct = 0;
    t.backgroundColor = d.backgroundColor;
    t.shadowColor = d.shadowColor;
    t.shadowOffsetPct = d.shadowOffsetPct;
    t.gradientColor = d.gradientColor;
    t.letterSpacingPct = 0;
    t.boxPaddingPct = d.boxPaddingPct;
    t.boxRadiusPct = d.boxRadiusPct;
    t.underline = false;
    t.textCase.clear();
    t.lineSpacing = d.lineSpacing;
    t.glowColor = d.glowColor;
    t.highlightColor = d.highlightColor;
}

void copyLook(const TitleProps &f, TitleProps &t)
{
    t.fontFamily = f.fontFamily;
    t.weight = f.weight;
    t.italic = f.italic;
    t.color = f.color;
    t.outlineColor = f.outlineColor;
    t.outlineWidthPct = f.outlineWidthPct;
    t.backgroundColor = f.backgroundColor;
    t.shadowColor = f.shadowColor;
    t.shadowOffsetPct = f.shadowOffsetPct;
    t.gradientColor = f.gradientColor;
    t.letterSpacingPct = f.letterSpacingPct;
    t.boxPaddingPct = f.boxPaddingPct;
    t.boxRadiusPct = f.boxRadiusPct;
    t.underline = f.underline;
    t.textCase = f.textCase;
    t.lineSpacing = f.lineSpacing;
    t.glowColor = f.glowColor;
    t.highlightColor = f.highlightColor;
    t.styleId = f.styleId;
}

// Font stacks: the first installed family wins (macOS names first, then Windows equivalents).
static const char *kSans = "Helvetica Neue, Segoe UI, Arial";
static const char *kSansBold = "Avenir Next, Segoe UI, Arial";
static const char *kHeavy = "Arial Black, Helvetica Neue, Arial";
static const char *kCondensed = "Impact, Haettenschweiler, Arial Narrow";
static const char *kGeometric = "Futura, Century Gothic, Avenir Next, Segoe UI";
static const char *kSerif = "Georgia, Cambria, Times New Roman";
static const char *kDidone = "Didot, Bodoni 72, Bodoni MT, Georgia";
static const char *kMono = "SF Mono, Menlo, Consolas, Courier New";
static const char *kHand = "Marker Felt, Segoe Print, Comic Sans MS";
static const char *kRounded = "Arial Rounded MT Bold, Nunito, Segoe UI";

QVector<TextStyle> all()
{
    auto S = [](const char *id, const char *label, const char *cat, std::function<void(TitleProps &)> fn) {
        return TextStyle{id, label, cat, std::move(fn)};
    };
    return {
        // ---- clean
        S("clean-white", "Clean white", "Clean", [](TitleProps &t) {
            t.fontFamily = kSans; t.weight = 700; t.shadowColor = QColor(0, 0, 0, 120); t.shadowOffsetPct = 0.04;
        }),
        S("clean-black", "Clean black", "Clean", [](TitleProps &t) {
            t.fontFamily = kSans; t.weight = 700; t.color = QColor("#111111");
        }),
        S("subtitle-classic", "Subtitle classic", "Clean", [](TitleProps &t) {
            t.fontFamily = kSans; t.weight = 600; t.outlineColor = Qt::black; t.outlineWidthPct = 0.06;
        }),
        S("minimal-caps", "Minimal caps", "Clean", [](TitleProps &t) {
            t.fontFamily = kGeometric; t.weight = 500; t.textCase = "upper"; t.letterSpacingPct = 18;
            t.shadowColor = QColor(0, 0, 0, 90); t.shadowOffsetPct = 0.03;
        }),
        S("documentary", "Documentary", "Clean", [](TitleProps &t) {
            t.fontFamily = kSans; t.weight = 300; t.textCase = "upper"; t.letterSpacingPct = 30;
            t.shadowColor = QColor(0, 0, 0, 140); t.shadowOffsetPct = 0.03;
        }),
        // ---- bold
        S("bold-outline", "Bold outline", "Bold", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.outlineColor = Qt::black; t.outlineWidthPct = 0.1;
            t.highlightColor = QColor("#FFD43B");
        }),
        S("youtube-pop", "YouTube pop", "Bold", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.textCase = "upper"; t.outlineColor = Qt::black;
            t.outlineWidthPct = 0.12; t.shadowColor = QColor(0, 0, 0, 200); t.shadowOffsetPct = 0.09;
            t.highlightColor = QColor("#FFE066");
        }),
        S("impact-meme", "Impact", "Bold", [](TitleProps &t) {
            t.fontFamily = kCondensed; t.weight = 400; t.textCase = "upper"; t.outlineColor = Qt::black; t.outlineWidthPct = 0.08;
        }),
        S("sports", "Sports", "Bold", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.italic = true; t.textCase = "upper"; t.color = QColor("#FFFFFF");
            t.gradientColor = QColor("#FFB703"); t.outlineColor = QColor("#1D1D1D"); t.outlineWidthPct = 0.07;
            t.shadowColor = QColor(0, 0, 0, 160); t.shadowOffsetPct = 0.08;
        }),
        S("retro-3d", "Retro 3D", "Bold", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.color = QColor("#FFD166"); t.outlineColor = QColor("#3A0CA3");
            t.outlineWidthPct = 0.06; t.shadowColor = QColor("#F72585"); t.shadowOffsetPct = 0.12;
        }),
        S("gold-gradient", "Gold gradient", "Bold", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.color = QColor("#FFF3B0"); t.gradientColor = QColor("#C9971C");
            t.shadowColor = QColor(0, 0, 0, 170); t.shadowOffsetPct = 0.06;
        }),
        // ---- boxed / labels
        S("box-dark", "Dark label", "Boxed", [](TitleProps &t) {
            t.fontFamily = kSansBold; t.weight = 600; t.backgroundColor = QColor(10, 10, 14, 200); t.boxPaddingPct = 0.45;
            t.boxRadiusPct = 0.14;
        }),
        S("box-white", "White label", "Boxed", [](TitleProps &t) {
            t.fontFamily = kSansBold; t.weight = 700; t.color = QColor("#111111"); t.backgroundColor = QColor(255, 255, 255, 240);
            t.boxPaddingPct = 0.45; t.boxRadiusPct = 0.14;
        }),
        S("marker", "Highlighter", "Boxed", [](TitleProps &t) {
            t.fontFamily = kSansBold; t.weight = 800; t.color = QColor("#111111"); t.backgroundColor = QColor("#FFE066");
            t.boxPaddingPct = 0.3; t.boxRadiusPct = 0.04;
        }),
        S("pill-blue", "Pill", "Boxed", [](TitleProps &t) {
            t.fontFamily = kRounded; t.weight = 700; t.backgroundColor = QColor("#4263EB"); t.boxPaddingPct = 0.7;
            t.boxRadiusPct = 0.9;
        }),
        S("news", "Breaking news", "Boxed", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.textCase = "upper"; t.backgroundColor = QColor("#E03131");
            t.boxPaddingPct = 0.4; t.boxRadiusPct = 0.0; t.letterSpacingPct = 4;
        }),
        S("glass", "Frosted glass", "Boxed", [](TitleProps &t) {
            t.fontFamily = kSans; t.weight = 500; t.backgroundColor = QColor(255, 255, 255, 60); t.boxPaddingPct = 0.6;
            t.boxRadiusPct = 0.4; t.shadowColor = QColor(0, 0, 0, 90); t.shadowOffsetPct = 0.03;
        }),
        // ---- glow
        S("neon-pink", "Neon pink", "Glow", [](TitleProps &t) {
            t.fontFamily = kGeometric; t.weight = 600; t.color = QColor("#FFE3F1"); t.glowColor = QColor("#FF3EA5");
        }),
        S("neon-cyan", "Neon cyan", "Glow", [](TitleProps &t) {
            t.fontFamily = kGeometric; t.weight = 600; t.color = QColor("#E3FDFF"); t.glowColor = QColor("#22D3EE");
        }),
        S("gamer", "Gamer", "Glow", [](TitleProps &t) {
            t.fontFamily = kHeavy; t.weight = 900; t.textCase = "upper"; t.color = QColor("#B9FBC0");
            t.glowColor = QColor("#38B000"); t.outlineColor = QColor("#081C15"); t.outlineWidthPct = 0.05;
        }),
        // ---- elegant
        S("cinematic", "Cinematic", "Elegant", [](TitleProps &t) {
            t.fontFamily = kDidone; t.weight = 400; t.textCase = "upper"; t.letterSpacingPct = 35;
            t.shadowColor = QColor(0, 0, 0, 150); t.shadowOffsetPct = 0.03;
        }),
        S("serif-quote", "Serif quote", "Elegant", [](TitleProps &t) {
            t.fontFamily = kSerif; t.weight = 400; t.italic = true; t.lineSpacing = 1.25;
            t.shadowColor = QColor(0, 0, 0, 120); t.shadowOffsetPct = 0.04; t.highlightColor = QColor("#E9C46A");
        }),
        S("luxury", "Luxury", "Elegant", [](TitleProps &t) {
            t.fontFamily = kDidone; t.weight = 400; t.textCase = "upper"; t.letterSpacingPct = 45;
            t.color = QColor("#F1E3C3"); t.gradientColor = QColor("#B08D57");
        }),
        S("magazine", "Magazine", "Elegant", [](TitleProps &t) {
            t.fontFamily = kSerif; t.weight = 700; t.color = QColor("#FFFFFF"); t.underline = true;
            t.shadowColor = QColor(0, 0, 0, 120); t.shadowOffsetPct = 0.04;
        }),
        // ---- fun
        S("handwritten", "Handwritten", "Fun", [](TitleProps &t) {
            t.fontFamily = kHand; t.weight = 400; t.color = QColor("#FFF8E7"); t.shadowColor = QColor(0, 0, 0, 140);
            t.shadowOffsetPct = 0.05;
        }),
        S("comic", "Comic", "Fun", [](TitleProps &t) {
            t.fontFamily = kHand; t.weight = 700; t.textCase = "upper"; t.color = QColor("#FFE066");
            t.outlineColor = Qt::black; t.outlineWidthPct = 0.1; t.shadowColor = QColor("#E8590C"); t.shadowOffsetPct = 0.1;
        }),
        S("typewriter", "Typewriter", "Fun", [](TitleProps &t) {
            t.fontFamily = kMono; t.weight = 500; t.color = QColor("#F8F9FA"); t.backgroundColor = QColor(0, 0, 0, 150);
            t.boxPaddingPct = 0.35; t.boxRadiusPct = 0.05;
        }),
        S("candy", "Candy", "Fun", [](TitleProps &t) {
            t.fontFamily = kRounded; t.weight = 800; t.color = QColor("#FFFFFF"); t.gradientColor = QColor("#FF8FAB");
            t.outlineColor = QColor("#7B2CBF"); t.outlineWidthPct = 0.07;
        }),
    };
}

const TextStyle *find(const QString &id)
{
    static const QVector<TextStyle> styles = all();
    for (const TextStyle &s : styles)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool apply(TitleProps &tp, const QString &id)
{
    const TextStyle *s = find(id);
    if (!s)
        return false;
    resetLook(tp);
    s->apply(tp);
    tp.styleId = id;
    return true;
}

} // namespace TextStyles
} // namespace ss
