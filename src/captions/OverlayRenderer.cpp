#include "OverlayRenderer.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace ss {

namespace {

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double easeOutCubic(double x) { return 1 - std::pow(1 - clamp01(x), 3); }
double easeOutBack(double x)
{
    x = clamp01(x);
    const double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}

struct WordLayout {
    QString text;
    QRectF rect;     // advance box (x, top, width, lineHeight)
    double baseline = 0;
    int idx = 0;     // index in cue.words
};

struct BlockLayout {
    QFont font;
    double fontPx = 0;
    double lineHeight = 0;
    QVector<QVector<WordLayout>> lines;
    QRectF block;
};

int activeWordIndex(const CueInstance &cue, TimeUs t)
{
    int last = -1;
    for (int i = 0; i < cue.words.size(); ++i) {
        if (t >= cue.words[i].start && t < cue.words[i].end)
            return i;
        if (cue.words[i].end <= t)
            last = i;
    }
    return last;
}

BlockLayout layoutBlock(const QSize &frame, const CaptionStyle &s, const QStringList &words, const QVector<int> &wordIdx,
                        int speaker)
{
    BlockLayout L;
    const QRectF safe = SafeArea::forFrame(frame.width(), frame.height());
    L.font = CaptionRenderer::fontFor(s, frame.height());
    L.fontPx = L.font.pixelSize();

    const auto groups = LineBreaker::breakLines(words, s.maxWordsPerLine, s.maxCharsPerLine, s.maxLines);
    const double maxW = safe.width() * s.maxWidthPct * (s.speakerPlacement == "alternateSides" ? 1.0 : 1.0);

    // shrink the font if any line is too wide for the safe area
    for (int attempt = 0; attempt < 6; ++attempt) {
        QFontMetricsF fm(L.font);
        double widest = 0;
        for (const auto &g : groups) {
            double w = 0;
            for (int k = 0; k < g.size(); ++k)
                w += fm.horizontalAdvance(words[g[k]]) + (k ? fm.horizontalAdvance(' ') : 0);
            widest = std::max(widest, w);
        }
        if (widest <= maxW || L.font.pixelSize() < 8)
            break;
        L.font.setPixelSize(std::max(8, int(L.font.pixelSize() * maxW / widest)));
    }
    L.fontPx = L.font.pixelSize();
    QFontMetricsF fm(L.font);
    L.lineHeight = fm.height() * s.lineSpacing;
    const double space = fm.horizontalAdvance(' ');
    const double blockH = L.lineHeight * groups.size();

    double y0;
    if (s.anchor == "top")
        y0 = safe.top();
    else if (s.anchor == "middle")
        y0 = safe.center().y() - blockH / 2;
    else
        y0 = safe.bottom() - blockH;
    y0 -= s.offsetYPct * frame.height();

    QString align = s.align;
    QRectF column = safe;
    if (s.speakerPlacement == "alternateSides" && speaker >= 0) {
        const double half = safe.width() / 2;
        column = speaker % 2 == 0 ? QRectF(safe.left(), safe.top(), half, safe.height())
                                  : QRectF(safe.left() + half, safe.top(), half, safe.height());
        align = speaker % 2 == 0 ? "left" : "right";
    }
    column.translate(s.offsetXPct * frame.width(), 0); // user-dragged horizontal position

    double minX = 1e9, maxX = -1e9;
    for (int li = 0; li < groups.size(); ++li) {
        const auto &g = groups[li];
        double lineW = 0;
        for (int k = 0; k < g.size(); ++k)
            lineW += fm.horizontalAdvance(words[g[k]]) + (k ? space : 0);
        double x;
        if (align == "left")
            x = column.left();
        else if (align == "right")
            x = column.right() - lineW;
        else
            x = column.center().x() - lineW / 2;
        const double top = y0 + li * L.lineHeight;
        const double baseline = top + (L.lineHeight - fm.height()) / 2 + fm.ascent();
        QVector<WordLayout> line;
        for (int k = 0; k < g.size(); ++k) {
            if (k)
                x += space;
            const double w = fm.horizontalAdvance(words[g[k]]);
            line << WordLayout{words[g[k]], QRectF(x, top, w, L.lineHeight), baseline, wordIdx.value(g[k], g[k])};
            minX = std::min(minX, x);
            maxX = std::max(maxX, x + w);
            x += w;
        }
        L.lines << line;
    }
    L.block = QRectF(minX, y0, std::max(0.0, maxX - minX), blockH);
    return L;
}

void drawTextPath(QPainter &p, const QPainterPath &path, const CaptionStyle &s, double fontPx, const QColor &fill,
                  double opacity, const QRectF &gradientRect = QRectF())
{
    auto withAlpha = [opacity](QColor c) {
        c.setAlphaF(c.alphaF() * opacity);
        return c;
    };
    // glow
    if (s.glowColor.alpha() > 0 && s.glowRadiusPct > 0) {
        const double r = s.glowRadiusPct * fontPx;
        for (int i = 3; i >= 1; --i) {
            QColor g = withAlpha(s.glowColor);
            g.setAlphaF(g.alphaF() * 0.22);
            QPen pen(g, r * i / 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.strokePath(path, pen);
        }
    }
    // shadow
    if (s.shadowColor.alpha() > 0 && s.shadowOffsetPct > 0) {
        const double off = s.shadowOffsetPct * fontPx;
        QPainterPath sh = path.translated(off, off);
        const QColor sc = withAlpha(s.shadowColor);
        if (s.outlineWidthPct > 0)
            p.strokePath(sh, QPen(sc, s.outlineWidthPct * fontPx * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(sh, sc);
    }
    // outline
    if (s.outlineColor.alpha() > 0 && s.outlineWidthPct > 0)
        p.strokePath(path, QPen(withAlpha(s.outlineColor), s.outlineWidthPct * fontPx * 2, Qt::SolidLine, Qt::RoundCap,
                                Qt::RoundJoin));
    if (gradientRect.isValid() && s.gradientColor.alpha() > 0) {
        QLinearGradient g(gradientRect.topLeft(), gradientRect.bottomLeft());
        g.setColorAt(0.15, withAlpha(fill));
        g.setColorAt(0.85, withAlpha(s.gradientColor));
        p.fillPath(path, g);
        return;
    }
    p.fillPath(path, withAlpha(fill));
}

} // namespace

QFont CaptionRenderer::fontFor(const CaptionStyle &s, int frameHeight)
{
    QFont f;
    f.setFamilies(s.fontFamilies);
    f.setPixelSize(std::max(6, int(std::lround(s.sizePct * frameHeight))));
    f.setWeight(QFont::Weight(std::clamp(s.fontWeight, 100, 900)));
    f.setItalic(s.italic);
    if (s.letterSpacingPct != 0)
        f.setLetterSpacing(QFont::PercentageSpacing, 100 + s.letterSpacingPct);
    f.setHintingPreference(QFont::PreferNoHinting); // resolution-independent: identical at any scale
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

static QStringList displayWords(const CaptionStyle &s, const CueInstance &cue)
{
    QStringList out;
    for (const auto &w : cue.words)
        out << (s.uppercase ? w.text.toUpper() : w.text);
    return out;
}

QRectF CaptionRenderer::cueRect(const QSize &frame, const CaptionStyle &s, const CueInstance &cue)
{
    const QStringList words = displayWords(s, cue);
    QVector<int> idx;
    for (int i = 0; i < words.size(); ++i)
        idx << i;
    return layoutBlock(frame, s, words, idx, cue.speaker).block;
}

void CaptionRenderer::paintCue(QPainter &p, const QSize &frame, const CaptionStyle &sIn, const CueInstance &cue, TimeUs t,
                               const SpeakerInfo *speaker)
{
    if (cue.words.isEmpty() || t < cue.start || t >= cue.end)
        return;
    CaptionStyle s = sIn.withOverrides(cue.overrides);
    if (s.speakerColors && speaker && speaker->color.isValid() && s.wordEffect != "none")
        s.activeColor = speaker->color;

    const QStringList allWords = displayWords(s, cue);
    const int active = activeWordIndex(cue, t);

    // Which words participate in layout
    QStringList words;
    QVector<int> idx;
    if (s.wordEffect == "activeOnly") {
        const int a = std::max(0, active);
        words << allWords[a];
        idx << a;
    } else {
        words = allWords;
        for (int i = 0; i < words.size(); ++i)
            idx << i;
    }
    BlockLayout L = layoutBlock(frame, s, words, idx, cue.speaker);

    // cue-level animation
    const double animUs = std::max(1, s.animMs) * double(kUsPerMs);
    const double aIn = s.animMs > 0 ? clamp01((t - cue.start) / animUs) : 1.0;
    const double aOut = s.animMs > 0 ? clamp01((cue.end - t) / animUs) : 1.0;
    double opacity = 1.0;
    double scale = 1.0;
    double dy = 0;
    if (s.entry == "fade")
        opacity *= easeOutCubic(aIn);
    else if (s.entry == "pop") {
        scale = 0.75 + 0.25 * easeOutBack(aIn);
        opacity *= clamp01(aIn * 2);
    } else if (s.entry == "slideUp") {
        dy = (1 - easeOutCubic(aIn)) * L.lineHeight * 0.6;
        opacity *= easeOutCubic(aIn);
    }
    if (s.exit == "fade")
        opacity *= easeOutCubic(aOut);
    if (opacity <= 0.001)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const QPointF center = L.block.center();
    p.translate(center.x(), center.y() + dy);
    p.scale(scale, scale);
    p.translate(-center.x(), -center.y());

    const double pad = s.boxPaddingPct * L.fontPx;
    const double radius = s.boxRadiusPct * L.fontPx;
    auto boxColor = [&]() {
        QColor c = s.boxColor;
        c.setAlphaF(c.alphaF() * opacity);
        return c;
    };

    // speaker label
    const bool showLabel = s.showSpeakerLabel && speaker && !speaker->name.isEmpty();
    // boxes
    if (s.boxMode == "cue") {
        QRectF r = L.block.adjusted(-pad, -pad * 0.6, pad, pad * 0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(boxColor());
        p.drawRoundedRect(r, radius, radius);
    } else if (s.boxMode == "line") {
        p.setPen(Qt::NoPen);
        p.setBrush(boxColor());
        for (const auto &line : L.lines) {
            if (line.isEmpty())
                continue;
            QRectF r(line.first().rect.left(), line.first().rect.top(), line.last().rect.right() - line.first().rect.left(),
                     L.lineHeight);
            p.drawRoundedRect(r.adjusted(-pad, 0, pad, 0), radius, radius);
        }
    } else if (s.boxMode == "bar") {
        QRectF r(0, L.block.top() - pad * 0.6, frame.width(), L.block.height() + pad * 1.2);
        p.setPen(Qt::NoPen);
        p.setBrush(boxColor());
        p.drawRect(r);
    }

    if (showLabel) {
        QFont lf = L.font;
        lf.setPixelSize(std::max(6, int(L.fontPx * 0.55)));
        lf.setWeight(QFont::Bold);
        QFontMetricsF lfm(lf);
        const QString label = speaker->name.toUpper();
        const double lw = lfm.horizontalAdvance(label);
        double lx = L.block.left();
        if (s.align == "center" && s.speakerPlacement != "alternateSides")
            lx = L.block.center().x() - lw / 2;
        const double ly = L.block.top() - (s.boxMode == "cue" ? pad * 0.6 : 0) - lfm.height() * 1.15;
        QColor lc = speaker->color.isValid() ? speaker->color : s.activeColor;
        QRectF lr(lx - lfm.height() * 0.35, ly, lw + lfm.height() * 0.7, lfm.height() * 1.05);
        QColor bg = lc;
        bg.setAlphaF(0.92 * opacity);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(lr, lr.height() * 0.3, lr.height() * 0.3);
        QPainterPath lp;
        lp.addText(lx, ly + (lr.height() - lfm.height()) / 2 + lfm.ascent(), lf, label);
        QColor tc = lc.lightnessF() > 0.6 ? QColor(20, 20, 20) : Qt::white;
        tc.setAlphaF(opacity);
        p.fillPath(lp, tc);
    }

    for (const auto &line : L.lines) {
        for (const WordLayout &wl : line) {
            const int wi = wl.idx;
            const WordInstance &w = cue.words[wi];
            const bool isActive = wi == active && t < w.end + 1;
            const bool spoken = w.end <= t;
            const bool started = w.start <= t;
            QColor fill = s.textColor;
            double wordScale = 1.0;
            QString text = wl.text;

            if (s.wordEffect == "reveal" && !started)
                continue;
            if (s.wordEffect == "typewriter") {
                if (!started)
                    continue;
                if (!spoken) {
                    const double prog = clamp01(double(t - w.start) / std::max<TimeUs>(1, w.end - w.start));
                    text = text.left(std::max(1, int(std::ceil(text.size() * prog))));
                }
            }
            if (s.wordEffect == "highlight" && wi == active)
                fill = s.activeColor;
            if (s.wordEffect == "pop" && isActive) {
                const double k = clamp01(double(t - w.start) / (animUs > 0 ? animUs : 1));
                wordScale = 1 + (s.popScale - 1) * (k < 1 ? easeOutBack(k) : 1.0);
                fill = s.activeColor;
            }
            if (s.wordEffect == "activeOnly") {
                const double k = clamp01(double(t - w.start) / animUs);
                wordScale = 0.6 + (s.popScale - 0.6) * easeOutBack(k);
                if (k >= 1)
                    wordScale = 1.0 + (s.popScale - 1.0) * std::max(0.0, 1 - double(t - w.start - animUs) / animUs);
                wordScale = std::max(0.6, wordScale);
            }

            const double tilt = (wi == active && s.activeRotateDeg != 0) ? s.activeRotateDeg : 0.0;
            QTransform tr;
            if (wordScale != 1.0 || tilt != 0) {
                const QPointF c = wl.rect.center();
                tr.translate(c.x(), c.y());
                tr.rotate(tilt);
                tr.scale(wordScale, wordScale);
                tr.translate(-c.x(), -c.y());
            }

            if (s.boxMode == "activeWord" && wi == active) {
                QRectF r = wl.rect.adjusted(-pad, pad * 0.1, pad, -pad * 0.1);
                p.save();
                p.setTransform(tr, true);
                p.setPen(Qt::NoPen);
                p.setBrush(boxColor());
                p.drawRoundedRect(r, radius, radius);
                p.restore();
            }

            QPainterPath path;
            path.addText(wl.rect.left(), wl.baseline, L.font, text);
            if (!tr.isIdentity())
                path = tr.map(path);
            const QRectF gradRect = fill == s.textColor ? tr.mapRect(wl.rect.adjusted(0, L.lineHeight * 0.12, 0, -L.lineHeight * 0.12)) : QRectF();

            if (s.wordEffect == "karaoke") {
                drawTextPath(p, path, s, L.fontPx, s.textColor, opacity, gradRect);
                double prog = spoken ? 1.0
                                     : (started ? clamp01(double(t - w.start) / std::max<TimeUs>(1, w.end - w.start)) : 0.0);
                if (prog > 0) {
                    p.save();
                    QRectF clipR = wl.rect.adjusted(-L.fontPx, -L.fontPx, 0, L.fontPx);
                    clipR.setRight(wl.rect.left() + wl.rect.width() * prog);
                    p.setClipRect(clipR);
                    QColor ac = s.activeColor;
                    ac.setAlphaF(ac.alphaF() * opacity);
                    p.fillPath(path, ac);
                    p.restore();
                }
            } else {
                drawTextPath(p, path, s, L.fontPx, fill, opacity, gradRect);
            }
        }
    }
    p.restore();
}

// ------------------------------------------------------------------ graphics

double GraphicsRenderer::clipOpacity(const Clip &c, TimeUs t)
{
    double o = std::clamp(c.transform.opacity, 0.0, 1.0);
    if (c.fadeIn > 0 && t < c.start + c.fadeIn)
        o *= clamp01(double(t - c.start) / c.fadeIn);
    if (c.fadeOut > 0 && t > c.end() - c.fadeOut)
        o *= clamp01(double(c.end() - t) / c.fadeOut);
    return o;
}

QFont GraphicsRenderer::titleFont(const TitleProps &tp, int frameHeight)
{
    QFont f;
    f.setFamilies({tp.fontFamily, "Helvetica Neue", "Arial", "Segoe UI", "DejaVu Sans"});
    f.setPixelSize(std::max(6, int(std::lround(tp.sizePct * frameHeight))));
    f.setWeight(QFont::Weight(std::clamp(tp.weight, 100, 900)));
    f.setItalic(tp.italic);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QRectF GraphicsRenderer::titleRect(const QSize &frame, const Clip &c)
{
    const TitleProps &tp = c.title;
    const QFont f = titleFont(tp, frame.height());
    QFontMetricsF fm(f);
    const QStringList lines = (tp.text.isEmpty() ? QString(" ") : tp.text).split('\n');
    double maxW = 0;
    for (const QString &l : lines)
        maxW = std::max(maxW, fm.horizontalAdvance(l));
    const double lh = fm.height() * 1.1;
    const double blockH = lh * lines.size();
    const QPointF ctr(tp.x * frame.width(), tp.y * frame.height());
    QRectF block(ctr.x() - maxW / 2, ctr.y() - blockH / 2, maxW, blockH);
    if (tp.backgroundColor.alpha() > 0) {
        const double pad = f.pixelSize() * 0.4;
        block.adjust(-pad, -pad * 0.5, pad, pad * 0.5);
    }
    return block;
}

QRectF GraphicsRenderer::shapeRect(const QSize &frame, const Clip &c)
{
    const ShapeProps &s = c.shape;
    QRectF r((s.x - s.w / 2) * frame.width(), (s.y - s.h / 2) * frame.height(), s.w * frame.width(), s.h * frame.height());
    if (s.shape == "line" && r.height() < 6) // keep thin lines grabbable
        r.adjust(0, -(6 - r.height()) / 2, 0, (6 - r.height()) / 2);
    return r;
}

void GraphicsRenderer::paintTitle(QPainter &p, const QSize &frame, const Clip &c, TimeUs t)
{
    const TitleProps &tp = c.title;
    if (!c.containsTimeline(t) || tp.text.isEmpty())
        return;
    double opacity = clipOpacity(c, t);
    const double ad = std::max<TimeUs>(1, tp.animationDuration);
    const double aIn = clamp01((t - c.start) / ad);
    const double aOut = clamp01((c.end() - t) / ad);
    double dy = 0, scale = 1;
    QString text = tp.text;
    if (tp.animation == "fade") {
        opacity *= easeOutCubic(aIn) * easeOutCubic(aOut);
    } else if (tp.animation == "slideUp") {
        dy = (1 - easeOutCubic(aIn)) * frame.height() * 0.04;
        opacity *= easeOutCubic(aIn) * easeOutCubic(aOut);
    } else if (tp.animation == "pop") {
        scale = 0.7 + 0.3 * easeOutBack(aIn);
        opacity *= clamp01(aIn * 2) * easeOutCubic(aOut);
    } else if (tp.animation == "typewriter") {
        const double typeDur = std::max<double>(ad, double(c.duration()) * 0.4);
        const int n = int(std::ceil(text.size() * clamp01((t - c.start) / typeDur)));
        text = text.left(std::max(0, n));
        opacity *= easeOutCubic(aOut);
    }
    if (opacity <= 0.001 || text.isEmpty())
        return;

    const QFont f = titleFont(tp, frame.height());
    QFontMetricsF fm(f);
    const QStringList lines = text.split('\n');
    const QStringList fullLines = tp.text.split('\n'); // stable layout during typewriter
    double maxW = 0;
    for (const QString &l : fullLines)
        maxW = std::max(maxW, fm.horizontalAdvance(l));
    const double lh = fm.height() * 1.1;
    const double blockH = lh * fullLines.size();
    const double cx = tp.x * frame.width();
    const double cy = tp.y * frame.height() + dy;
    const QRectF block(cx - maxW / 2, cy - blockH / 2, maxW, blockH);

    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(cx, cy);
    p.scale(scale, scale);
    p.translate(-cx, -cy);
    p.setOpacity(opacity);
    if (tp.backgroundColor.alpha() > 0) {
        const double pad = f.pixelSize() * 0.4;
        p.setPen(Qt::NoPen);
        p.setBrush(tp.backgroundColor);
        p.drawRoundedRect(block.adjusted(-pad, -pad * 0.5, pad, pad * 0.5), pad * 0.4, pad * 0.4);
    }
    for (int i = 0; i < lines.size(); ++i) {
        const double lw = fm.horizontalAdvance(fullLines.value(i));
        double x = block.left();
        if (tp.align == "center")
            x = cx - lw / 2;
        else if (tp.align == "right")
            x = block.right() - lw;
        const double baseline = block.top() + i * lh + (lh - fm.height()) / 2 + fm.ascent();
        QPainterPath path;
        path.addText(x, baseline, f, lines[i]);
        if (tp.outlineColor.alpha() > 0 && tp.outlineWidthPct > 0)
            p.strokePath(path, QPen(tp.outlineColor, tp.outlineWidthPct * f.pixelSize() * 2, Qt::SolidLine, Qt::RoundCap,
                                    Qt::RoundJoin));
        p.fillPath(path, tp.color);
    }
    p.restore();
}

void GraphicsRenderer::paintShape(QPainter &p, const QSize &frame, const Clip &c, TimeUs t)
{
    if (!c.containsTimeline(t))
        return;
    const ShapeProps &s = c.shape;
    const double opacity = clipOpacity(c, t);
    if (opacity <= 0.001)
        return;
    const QRectF r((s.x - s.w / 2) * frame.width(), (s.y - s.h / 2) * frame.height(), s.w * frame.width(),
                   s.h * frame.height());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(opacity);
    const double sw = s.strokeWidthPct * frame.height();
    p.setPen(s.stroke.alpha() > 0 && sw > 0 ? QPen(s.stroke, sw) : QPen(Qt::NoPen));
    p.setBrush(s.fill);
    if (s.shape == "ellipse")
        p.drawEllipse(r);
    else if (s.shape == "arrow") {
        // right-pointing arrow filling the box: shaft 40% of the height, head 35% of the width
        const double head = std::min(r.width() * 0.45, r.height() * 1.1);
        const double shaft = r.height() * 0.38;
        QPainterPath a;
        a.moveTo(r.left(), r.center().y() - shaft / 2);
        a.lineTo(r.right() - head, r.center().y() - shaft / 2);
        a.lineTo(r.right() - head, r.top());
        a.lineTo(r.right(), r.center().y());
        a.lineTo(r.right() - head, r.bottom());
        a.lineTo(r.right() - head, r.center().y() + shaft / 2);
        a.lineTo(r.left(), r.center().y() + shaft / 2);
        a.closeSubpath();
        p.setPen(p.pen().style() == Qt::NoPen ? QPen(Qt::NoPen) : QPen(s.stroke, sw, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawPath(a);
    } else if (s.shape == "triangle") {
        QPolygonF tri{QPointF(r.center().x(), r.top()), r.bottomRight(), r.bottomLeft()};
        p.drawPolygon(tri);
    } else if (s.shape == "star") {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double ang = -M_PI / 2 + i * M_PI / 5;
            const double k = i % 2 ? 0.42 : 1.0;
            star << QPointF(r.center().x() + std::cos(ang) * r.width() / 2 * k, r.center().y() + std::sin(ang) * r.height() / 2 * k);
        }
        p.drawPolygon(star);
    }
    else if (s.shape == "line") {
        p.setPen(QPen(s.fill, std::max(1.0, r.height())));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    } else {
        const double rad = s.radiusPct * std::min(r.width(), r.height());
        p.drawRoundedRect(r, rad, rad);
    }
    p.restore();
}

// ------------------------------------------------------------------ scene

OverlayScene OverlayScene::build(const Project &p, bool includeCaptions, bool onlyBurnIn)
{
    OverlayScene sc;
    sc.speakers = p.speakers;
    for (const auto &t : p.tracks) {
        if (t.kind == TrackKind::Video && !t.hidden) {
            for (const auto &c : t.clips)
                if (c.enabled && (c.type == ClipType::Title || c.type == ClipType::Shape))
                    sc.graphics << GraphicLayer{c};
        }
    }
    if (includeCaptions) {
        for (const auto &t : p.tracks) {
            if (t.kind != TrackKind::Caption || t.hidden || (onlyBurnIn && !t.burnIn))
                continue;
            CaptionLayer L;
            L.trackId = t.id;
            L.style = TemplateLibrary::instance().get(t.styleId).withOverrides(t.styleOverrides);
            L.cues = CaptionMapper::map(p, t);
            L.burnIn = t.burnIn;
            sc.captions << L;
        }
    }
    return sc;
}

bool OverlayScene::isEmpty() const
{
    bool any = !graphics.isEmpty();
    for (const auto &c : captions)
        any = any || !c.cues.isEmpty();
    return !any;
}

bool OverlayScene::isEmptyAt(TimeUs t) const
{
    for (const auto &g : graphics)
        if (g.clip.containsTimeline(t))
            return false;
    for (const auto &c : captions)
        if (CaptionMapper::instanceAt(c.cues, t))
            return false;
    return true;
}

void OverlayScene::render(QPainter &p, const QSize &frame, TimeUs t) const
{
    for (const auto &g : graphics) {
        if (!g.clip.containsTimeline(t))
            continue;
        if (g.clip.type == ClipType::Title)
            GraphicsRenderer::paintTitle(p, frame, g.clip, t);
        else
            GraphicsRenderer::paintShape(p, frame, g.clip, t);
    }
    for (const auto &c : captions) {
        const CueInstance *ci = CaptionMapper::instanceAt(c.cues, t);
        if (!ci)
            continue;
        CaptionRenderer::SpeakerInfo info;
        const CaptionRenderer::SpeakerInfo *infoPtr = nullptr;
        if (ci->speaker >= 0) {
            for (const auto &s : speakers)
                if (s.id == ci->speaker) {
                    info.name = s.name;
                    info.color = s.color;
                    infoPtr = &info;
                }
        }
        CaptionRenderer::paintCue(p, frame, c.style, *ci, t, infoPtr);
    }
}

QImage OverlayScene::renderImage(const QSize &frame, TimeUs t) const
{
    QImage img(frame, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (isEmptyAt(t))
        return img;
    QPainter p(&img);
    render(p, frame, t);
    p.end();
    return img;
}

} // namespace ss
