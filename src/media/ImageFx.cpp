#include "ImageFx.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLinearGradient>
#include <QMutex>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QTextStream>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace ss {
namespace ImageFx {

namespace {

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline double smoothstep(double a, double b, double x)
{
    const double t = clamp01((x - a) / std::max(1e-9, b - a));
    return t * t * (3 - 2 * t);
}

void ensurePremul(QImage &img)
{
    if (img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Draws an image centred on the canvas with an extra scale/rotation/offset/opacity.
void drawXform(QPainter &p, const QImage &img, double scale, double rotation, QPointF offset, double opacity)
{
    if (img.isNull() || opacity <= 0.001 || scale <= 0.0001)
        return;
    p.save();
    p.setOpacity(p.opacity() * opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF c(img.width() / 2.0, img.height() / 2.0);
    p.translate(c + offset);
    p.rotate(rotation);
    p.scale(scale, scale);
    p.translate(-c);
    p.drawImage(0, 0, img);
    p.restore();
}

double easeInOut(double t)
{
    t = clamp01(t);
    return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

double smoothNoise(double t, quint32 salt)
{
    // value noise in [-1, 1]
    const double f = std::floor(t);
    const double u = t - f;
    const double a = (hash32(quint32(qint64(f)) * 2654435761u ^ salt) & 0xffff) / 32767.5 - 1.0;
    const double b = (hash32(quint32(qint64(f) + 1) * 2654435761u ^ salt) & 0xffff) / 32767.5 - 1.0;
    const double s = u * u * (3 - 2 * u);
    return a + (b - a) * s;
}

} // namespace

quint32 hash32(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// ------------------------------------------------------------------ blur

void boxBlur(QImage &img, int r, int passes)
{
    if (img.isNull() || r < 1)
        return;
    ensurePremul(img);
    const int w = img.width(), h = img.height();
    const int stride = int(img.bytesPerLine() / 4);
    quint32 *bits = reinterpret_cast<quint32 *>(img.bits());
    QVector<quint32> line(std::max(w, h));
    auto blurLine = [&](quint32 *base, int n, int step) {
        for (int i = 0; i < n; ++i)
            line[i] = base[i * step];
        const int win = 2 * r + 1;
        int sa = 0, sr = 0, sg = 0, sb = 0;
        auto add = [&](quint32 px, int sign) {
            sa += sign * int(qAlpha(px));
            sr += sign * int(qRed(px));
            sg += sign * int(qGreen(px));
            sb += sign * int(qBlue(px));
        };
        for (int i = -r; i <= r; ++i)
            add(line[std::clamp(i, 0, n - 1)], 1);
        for (int x = 0; x < n; ++x) {
            base[x * step] = qRgba(sr / win, sg / win, sb / win, sa / win);
            add(line[std::clamp(x + r + 1, 0, n - 1)], 1);
            add(line[std::clamp(x - r, 0, n - 1)], -1);
        }
    };
    for (int p = 0; p < passes; ++p) {
        for (int y = 0; y < h; ++y)
            blurLine(bits + y * stride, w, 1);
        for (int x = 0; x < w; ++x)
            blurLine(bits + x, h, stride);
    }
}

QImage blurred(const QImage &src, double radius)
{
    if (src.isNull() || radius < 0.5)
        return src;
    QImage img = src.format() == QImage::Format_ARGB32_Premultiplied ? src : src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int factor = std::max(1, int(radius / 3));
    if (factor == 1) {
        QImage out = img.copy();
        boxBlur(out, std::max(1, int(std::lround(radius))));
        return out;
    }
    QImage small = img.scaled(std::max(1, img.width() / factor), std::max(1, img.height() / factor), Qt::IgnoreAspectRatio,
                              Qt::SmoothTransformation);
    boxBlur(small, std::max(1, int(std::lround(radius / factor))));
    return small.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// ------------------------------------------------------------------ chroma key

void chromaKey(QImage &img, const ChromaKey &ck)
{
    if (!ck.enabled || img.isNull())
        return;
    ensurePremul(img);
    const double kr = ck.color.redF(), kg = ck.color.greenF(), kb = ck.color.blueF();
    const double ky = 0.299 * kr + 0.587 * kg + 0.114 * kb;
    const double kcb = (kb - ky) * 0.564, kcr = (kr - ky) * 0.713;
    const double t0 = std::clamp(ck.similarity, 0.0, 1.0) * 0.45;
    const double t1 = t0 + std::max(0.004, std::clamp(ck.smoothness, 0.0, 1.0) * 0.4);
    const int dom = (kg >= kr && kg >= kb) ? 1 : (kb >= kr ? 2 : 0); // dominant key channel
    const double spill = std::clamp(ck.spill, 0.0, 1.0);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = row[x];
            const int a = qAlpha(px);
            if (a == 0)
                continue;
            double r = qRed(px), g = qGreen(px), b = qBlue(px);
            if (a < 255) {
                r = r * 255 / a;
                g = g * 255 / a;
                b = b * 255 / a;
            }
            const double yy = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            const double cb = (b / 255.0 - yy) * 0.564, cr = (r / 255.0 - yy) * 0.713;
            const double d = std::hypot(cb - kcb, cr - kcr);
            const double keep = smoothstep(t0, t1, d);
            if (keep <= 0.0) {
                row[x] = 0;
                continue;
            }
            double c[3] = {r, g, b};
            const double others = std::max(c[(dom + 1) % 3], c[(dom + 2) % 3]);
            if (c[dom] > others)
                c[dom] = others + (c[dom] - others) * (1 - spill);
            const double na = a * keep;
            const double k = na / 255.0;
            row[x] = qRgba(clamp255(int(c[0] * k)), clamp255(int(c[1] * k)), clamp255(int(c[2] * k)), clamp255(int(na)));
        }
    }
}

// ------------------------------------------------------------------ masks

void applyMask(QImage &img, const MaskSpec &m)
{
    if (!m.isSet() || img.isNull())
        return;
    ensurePremul(img);
    const double W = img.width(), H = img.height();
    const double featherPx = std::max(0.0, m.feather) * std::min(W, H);
    QImage mask(img.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter q(&mask);
        q.setRenderHint(QPainter::Antialiasing);
        q.translate(m.cx * W, m.cy * H);
        q.rotate(m.angle);
        q.setPen(Qt::NoPen);
        const double big = (W + H) * 2;
        if (m.type == "circle") {
            const double rx = std::max(1.0, m.w * W / 2), ry = std::max(1.0, m.h * H / 2);
            q.scale(1.0, ry / rx);
            QRadialGradient g(QPointF(0, 0), rx + featherPx / 2);
            const double inner = std::clamp((rx - featherPx / 2) / (rx + featherPx / 2), 0.0, 0.999);
            g.setColorAt(0, Qt::white);
            g.setColorAt(inner, Qt::white);
            g.setColorAt(1, Qt::transparent);
            q.setBrush(g);
            q.drawEllipse(QPointF(0, 0), rx + featherPx / 2, rx + featherPx / 2);
        } else if (m.type == "rect") {
            const double rw = m.w * W, rh = m.h * H;
            const double rad = std::clamp(m.radius, 0.0, 0.5) * std::min(rw, rh);
            q.setBrush(Qt::white);
            q.drawRoundedRect(QRectF(-rw / 2, -rh / 2, rw, rh), rad, rad);
        } else if (m.type == "mirror") {
            const double band = std::max(1.0, m.h * H / 2);
            QLinearGradient g(QPointF(0, -band - featherPx / 2), QPointF(0, band + featherPx / 2));
            const double tot = 2 * band + featherPx;
            const double f = featherPx / tot;
            g.setColorAt(0, Qt::transparent);
            g.setColorAt(std::clamp(f, 0.0, 0.49), Qt::white);
            g.setColorAt(std::clamp(1 - f, 0.51, 1.0), Qt::white);
            g.setColorAt(1, Qt::transparent);
            q.setBrush(g);
            q.drawRect(QRectF(-big, -band - featherPx / 2, 2 * big, tot));
        } else { // linear: keep the side below the line
            QLinearGradient g(QPointF(0, -featherPx / 2 - 0.5), QPointF(0, featherPx / 2 + 0.5));
            g.setColorAt(0, Qt::transparent);
            g.setColorAt(1, Qt::white);
            q.setBrush(g);
            q.drawRect(QRectF(-big, -featherPx / 2 - 0.5, 2 * big, big));
        }
    }
    if (m.type == "rect" && featherPx >= 1)
        boxBlur(mask, std::max(1, int(featherPx / 3)));
    if (m.invert) {
        QImage inv(img.size(), QImage::Format_ARGB32_Premultiplied);
        inv.fill(Qt::white);
        QPainter q(&inv);
        q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        q.drawImage(0, 0, mask);
        q.end();
        mask = inv;
    }
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
}

// ------------------------------------------------------------------ colour adjust

void colorAdjust(QImage &img, const ExtraAdjust &a, quint32 seed)
{
    if (img.isNull())
        return;
    ensurePremul(img);
    if (a.sharpen > 0.001) {
        QImage soft = img.copy();
        boxBlur(soft, std::max(1, img.width() / 900), 1);
        const double k = a.sharpen * 1.6;
        for (int y = 0; y < img.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            const QRgb *s = reinterpret_cast<const QRgb *>(soft.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb p = row[x], q = s[x];
                const int al = qAlpha(p);
                row[x] = qRgba(std::min(al, clamp255(int(qRed(p) + k * (qRed(p) - qRed(q))))),
                               std::min(al, clamp255(int(qGreen(p) + k * (qGreen(p) - qGreen(q))))),
                               std::min(al, clamp255(int(qBlue(p) + k * (qBlue(p) - qBlue(q))))), al);
            }
        }
    }
    const bool tone = a.highlights != 0 || a.shadows != 0 || a.fade != 0;
    const bool hue = a.hue != 0;
    const bool vib = a.vibrance != 0;
    const bool vig = a.vignette > 0.001;
    const bool grain = a.grain > 0.001;
    if (!tone && !hue && !vib && !vig && !grain)
        return;

    int curve[256];
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        v += a.shadows * 0.45 * std::pow(1 - v, 2.5) * v * 2.2;      // lift/crush the darks
        v += a.highlights * 0.45 * std::pow(v, 2.5) * (1 - v) * 2.2; // boost/recover the brights
        v = v * (1 - 0.18 * a.fade) + 0.12 * a.fade;                 // matte / faded blacks
        curve[i] = clamp255(int(std::lround(v * 255)));
    }
    // hue rotation in YIQ space
    const double th = qDegreesToRadians(a.hue), cs = std::cos(th), sn = std::sin(th);
    double M[3][3];
    {
        const double toY[3][3] = {{0.299, 0.587, 0.114}, {0.596, -0.274, -0.322}, {0.211, -0.523, 0.312}};
        const double fromY[3][3] = {{1, 0.956, 0.621}, {1, -0.272, -0.647}, {1, -1.106, 1.703}};
        const double R[3][3] = {{1, 0, 0}, {0, cs, -sn}, {0, sn, cs}};
        double T[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                T[i][j] = 0;
                for (int k = 0; k < 3; ++k)
                    T[i][j] += R[i][k] * toY[k][j];
            }
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                M[i][j] = 0;
                for (int k = 0; k < 3; ++k)
                    M[i][j] += fromY[i][k] * T[k][j];
            }
    }
    const double cx = img.width() / 2.0, cy = img.height() / 2.0;
    const double grainAmp = a.grain * 70;
    for (int y = 0; y < img.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        const double dy = (y - cy) / cy;
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = row[x];
            const int al = qAlpha(px);
            if (al == 0)
                continue;
            double r = qRed(px), g = qGreen(px), b = qBlue(px);
            if (al < 255) {
                r = r * 255 / al;
                g = g * 255 / al;
                b = b * 255 / al;
            }
            if (tone) {
                r = curve[clamp255(int(r))];
                g = curve[clamp255(int(g))];
                b = curve[clamp255(int(b))];
            }
            if (hue) {
                const double nr = M[0][0] * r + M[0][1] * g + M[0][2] * b;
                const double ng = M[1][0] * r + M[1][1] * g + M[1][2] * b;
                const double nb = M[2][0] * r + M[2][1] * g + M[2][2] * b;
                r = nr;
                g = ng;
                b = nb;
            }
            if (vib) {
                const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
                const double sat = (mx - mn) / 255.0;
                const double amt = a.vibrance * (1 - sat) * 1.2;
                const double mean = (r + g + b) / 3;
                r = mean + (r - mean) * (1 + amt);
                g = mean + (g - mean) * (1 + amt);
                b = mean + (b - mean) * (1 + amt);
            }
            if (vig) {
                const double dx = (x - cx) / cx;
                const double d = std::sqrt(dx * dx + dy * dy) / 1.4142;
                const double f = 1 - a.vignette * 0.85 * smoothstep(0.35, 1.0, d);
                r *= f;
                g *= f;
                b *= f;
            }
            if (grain) {
                const double n = (int(hash32(quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ seed) & 255) - 128) / 128.0;
                r += n * grainAmp;
                g += n * grainAmp;
                b += n * grainAmp;
            }
            const double k = al / 255.0;
            row[x] = qRgba(clamp255(int(r * k)), clamp255(int(g * k)), clamp255(int(b * k)), al);
        }
    }
}

// ------------------------------------------------------------------ LUT

namespace {
struct Lut {
    int n = 0;
    QVector<float> data; // r fastest, rgb triplets
    qint64 mtime = 0;
};
QMutex g_lutMutex;
QHash<QString, Lut> g_luts;

bool loadLut(const QString &path, Lut *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = "Cannot open LUT " + path;
        return false;
    }
    QTextStream ts(&f);
    Lut lut;
    double dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.first() == "LUT_3D_SIZE" && parts.size() >= 2) {
            lut.n = parts[1].toInt();
            lut.data.reserve(lut.n * lut.n * lut.n * 3);
        } else if (parts.first() == "DOMAIN_MIN" && parts.size() >= 4) {
            for (int i = 0; i < 3; ++i)
                dmin[i] = parts[i + 1].toDouble();
        } else if (parts.first() == "DOMAIN_MAX" && parts.size() >= 4) {
            for (int i = 0; i < 3; ++i)
                dmax[i] = parts[i + 1].toDouble();
        } else if (parts.size() == 3 && lut.n > 0) {
            bool ok0, ok1, ok2;
            const float r = parts[0].toFloat(&ok0), g = parts[1].toFloat(&ok1), b = parts[2].toFloat(&ok2);
            if (ok0 && ok1 && ok2)
                lut.data << float((r - dmin[0]) / (dmax[0] - dmin[0])) << float((g - dmin[1]) / (dmax[1] - dmin[1]))
                         << float((b - dmin[2]) / (dmax[2] - dmin[2]));
        }
    }
    if (lut.n < 2 || lut.data.size() != lut.n * lut.n * lut.n * 3) {
        if (error)
            *error = "Not a valid 3D .cube LUT: " + QFileInfo(path).fileName();
        return false;
    }
    *out = lut;
    return true;
}
} // namespace

bool applyLut(QImage &img, const QString &path, double strength, QString *error)
{
    if (path.isEmpty() || img.isNull() || strength <= 0)
        return true;
    Lut lut;
    {
        QMutexLocker lock(&g_lutMutex);
        const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
        auto it = g_luts.find(path);
        if (it == g_luts.end() || it->mtime != mtime) {
            Lut fresh;
            if (!loadLut(path, &fresh, error))
                return false;
            fresh.mtime = mtime;
            it = g_luts.insert(path, fresh);
        }
        lut = it.value();
    }
    ensurePremul(img);
    const int n = lut.n;
    const float *d = lut.data.constData();
    auto at = [&](int r, int g, int b, int c) { return d[((b * n + g) * n + r) * 3 + c]; };
    const double s = std::clamp(strength, 0.0, 1.0);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = row[x];
            const int al = qAlpha(px);
            if (al == 0)
                continue;
            double in[3] = {double(qRed(px)), double(qGreen(px)), double(qBlue(px))};
            if (al < 255)
                for (double &v : in)
                    v = v * 255 / al;
            double fr = in[0] / 255.0 * (n - 1), fg = in[1] / 255.0 * (n - 1), fb = in[2] / 255.0 * (n - 1);
            const int r0 = std::clamp(int(fr), 0, n - 2), g0 = std::clamp(int(fg), 0, n - 2), b0 = std::clamp(int(fb), 0, n - 2);
            fr -= r0;
            fg -= g0;
            fb -= b0;
            double out[3];
            for (int c = 0; c < 3; ++c) {
                const double c00 = at(r0, g0, b0, c) * (1 - fr) + at(r0 + 1, g0, b0, c) * fr;
                const double c10 = at(r0, g0 + 1, b0, c) * (1 - fr) + at(r0 + 1, g0 + 1, b0, c) * fr;
                const double c01 = at(r0, g0, b0 + 1, c) * (1 - fr) + at(r0 + 1, g0, b0 + 1, c) * fr;
                const double c11 = at(r0, g0 + 1, b0 + 1, c) * (1 - fr) + at(r0 + 1, g0 + 1, b0 + 1, c) * fr;
                const double v = (c00 * (1 - fg) + c10 * fg) * (1 - fb) + (c01 * (1 - fg) + c11 * fg) * fb;
                out[c] = in[c] + (v * 255 - in[c]) * s;
            }
            const double k = al / 255.0;
            row[x] = qRgba(clamp255(int(out[0] * k)), clamp255(int(out[1] * k)), clamp255(int(out[2] * k)), al);
        }
    }
    return true;
}

// ------------------------------------------------------------------ effects

QVector<Info> effects()
{
    return {
        {"blur", "Blur", "Basic"},
        {"dreamy", "Dreamy soft focus", "Basic"},
        {"glow", "Glow", "Basic"},
        {"vignette", "Vignette", "Basic"},
        {"black-white", "Black & white", "Basic"},
        {"sepia", "Sepia", "Basic"},
        {"invert", "Invert", "Basic"},
        {"letterbox", "Cinema bars", "Basic"},
        {"zoom-pulse", "Zoom pulse", "Motion"},
        {"shake", "Camera shake", "Motion"},
        {"ken-burns", "Slow zoom (Ken Burns)", "Motion"},
        {"flash", "Flash", "Party"},
        {"strobe", "Strobe", "Party"},
        {"color-cycle", "Colour cycle", "Party"},
        {"light-leak", "Light leak", "Party"},
        {"glitch", "Glitch", "Retro"},
        {"rgb-split", "RGB split", "Retro"},
        {"vhs", "VHS", "Retro"},
        {"scanlines", "Scanlines", "Retro"},
        {"grain", "Film grain", "Retro"},
        {"old-film", "Old film", "Retro"},
        {"pixelate", "Pixelate", "Stylize"},
        {"mirror", "Mirror", "Stylize"},
        {"kaleidoscope", "Kaleidoscope", "Stylize"},
    };
}

QString effectLabel(const QString &id)
{
    for (const Info &i : effects())
        if (i.id == id)
            return i.label;
    return id;
}

bool isMotionEffect(const QString &id)
{
    return id == "shake" || id == "zoom-pulse" || id == "ken-burns";
}

void effectMotion(const EffectSpec &e, double t, double prog, double *dx, double *dy, double *scale, double *rotation)
{
    const double k = std::clamp(e.intensity, 0.0, 1.0);
    const double sp = std::max(0.05, e.speed);
    if (e.type == "shake") {
        const double f = t * 9 * sp;
        *dx += smoothNoise(f, 11) * k * 0.025;
        *dy += smoothNoise(f, 23) * k * 0.025;
        *rotation += smoothNoise(f, 37) * k * 1.6;
        *scale *= 1 + k * 0.05; // hide the edges while shaking
    } else if (e.type == "zoom-pulse") {
        const double ph = t * sp * 2.0; // pulses per second = 2 * speed (≈ 120 BPM)
        const double beat = ph - std::floor(ph);
        *scale *= 1 + k * 0.12 * std::exp(-beat * 6.0);
    } else if (e.type == "ken-burns") {
        *scale *= 1 + k * 0.25 * clamp01(prog);
    }
}

namespace {

void rgbShift(QImage &img, int dx, int dy = 0)
{
    if (dx == 0 && dy == 0)
        return;
    const QImage src = img.copy();
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        const QRgb *rr = reinterpret_cast<const QRgb *>(src.constScanLine(std::clamp(y - dy, 0, h - 1)));
        const QRgb *rc = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        const QRgb *rb = reinterpret_cast<const QRgb *>(src.constScanLine(std::clamp(y + dy, 0, h - 1)));
        for (int x = 0; x < w; ++x) {
            const QRgb a = rr[std::clamp(x - dx, 0, w - 1)], c = rc[x], b = rb[std::clamp(x + dx, 0, w - 1)];
            const int al = std::max({qAlpha(a), qAlpha(c), qAlpha(b)});
            row[x] = qRgba(std::min(al, qRed(a)), std::min(al, qGreen(c)), std::min(al, qBlue(b)), al);
        }
    }
}

void perPixel(QImage &img, const std::function<void(int &, int &, int &, int, int)> &fn)
{
    for (int y = 0; y < img.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = row[x];
            const int al = qAlpha(px);
            if (al == 0)
                continue;
            int r = qRed(px), g = qGreen(px), b = qBlue(px);
            fn(r, g, b, x, y);
            row[x] = qRgba(std::min(al, clamp255(r)), std::min(al, clamp255(g)), std::min(al, clamp255(b)), al);
        }
    }
}

void addNoise(QImage &img, double amount, quint32 seed, bool mono = true)
{
    perPixel(img, [&](int &r, int &g, int &b, int x, int y) {
        const quint32 h = hash32(quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ seed);
        const int n = int(((int(h & 255) - 128) / 128.0) * amount);
        if (mono) {
            r += n;
            g += n;
            b += n;
        } else {
            r += n;
            g += int((int((h >> 8) & 255) - 128) / 128.0 * amount);
            b += int((int((h >> 16) & 255) - 128) / 128.0 * amount);
        }
    });
}

void tint(QImage &img, double amount, double rr, double gg, double bb)
{
    perPixel(img, [&](int &r, int &g, int &b, int, int) {
        const double l = 0.299 * r + 0.587 * g + 0.114 * b;
        r = int(r + (l * rr - r) * amount);
        g = int(g + (l * gg - g) * amount);
        b = int(b + (l * bb - b) * amount);
    });
}

void overlayFill(QImage &img, const QColor &c, double alpha)
{
    if (alpha <= 0.001)
        return;
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    QColor cc = c;
    cc.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    p.fillRect(img.rect(), cc);
}

} // namespace

void applyEffect(QImage &img, const EffectSpec &e, double t, double prog, quint32 seed)
{
    if (img.isNull() || isMotionEffect(e.type))
        return;
    ensurePremul(img);
    const double k = std::clamp(e.intensity, 0.0, 1.0);
    const double sp = std::max(0.05, e.speed);
    const int w = img.width(), h = img.height();
    const QString &id = e.type;

    if (id == "blur") {
        img = blurred(img, k * 0.03 * w);
    } else if (id == "dreamy") {
        const QImage b = blurred(img, 0.012 * w);
        QPainter p(&img);
        p.setOpacity(0.55 * k);
        p.drawImage(0, 0, b);
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        p.setOpacity(0.25 * k);
        p.drawImage(0, 0, b);
    } else if (id == "glow") {
        QImage b = blurred(img, 0.02 * w);
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        p.setOpacity(0.9 * k);
        p.drawImage(0, 0, b);
    } else if (id == "vignette") {
        ExtraAdjust a;
        a.vignette = k;
        colorAdjust(img, a, seed);
    } else if (id == "black-white") {
        tint(img, k, 1, 1, 1);
    } else if (id == "sepia") {
        tint(img, k, 1.07, 0.92, 0.72);
    } else if (id == "invert") {
        perPixel(img, [&](int &r, int &g, int &b, int, int) {
            r = int(r + (255 - 2 * r) * k);
            g = int(g + (255 - 2 * g) * k);
            b = int(b + (255 - 2 * b) * k);
        });
    } else if (id == "letterbox") {
        const int bar = int(h * 0.13 * k);
        QPainter p(&img);
        p.fillRect(0, 0, w, bar, Qt::black);
        p.fillRect(0, h - bar, w, bar, Qt::black);
    } else if (id == "flash") {
        const double ph = t * sp * 2.0;
        const double beat = ph - std::floor(ph);
        overlayFill(img, Qt::white, k * std::exp(-beat * 7.0));
    } else if (id == "strobe") {
        const bool off = (qint64(std::floor(t * sp * 8)) % 2) == 1;
        if (off)
            overlayFill(img, Qt::black, 0.7 * k);
    } else if (id == "color-cycle") {
        ExtraAdjust a;
        a.hue = std::fmod(t * sp * 120.0 * std::max(0.2, k), 360.0) - 180.0;
        a.vibrance = 0.3 * k;
        colorAdjust(img, a, seed);
    } else if (id == "light-leak") {
        QImage leak(img.size(), QImage::Format_ARGB32_Premultiplied);
        leak.fill(Qt::transparent);
        {
            QPainter q(&leak);
            const double ph = t * sp * 0.35;
            const QPointF c1(w * (0.15 + 0.25 * std::sin(ph)), h * (0.2 + 0.15 * std::cos(ph * 1.3)));
            const QPointF c2(w * (0.85 - 0.2 * std::sin(ph * 0.8)), h * (0.75 + 0.15 * std::sin(ph * 1.1)));
            QRadialGradient g1(c1, w * 0.55);
            g1.setColorAt(0, QColor(255, 140, 60, 220));
            g1.setColorAt(1, QColor(255, 60, 120, 0));
            q.fillRect(leak.rect(), g1);
            QRadialGradient g2(c2, w * 0.45);
            g2.setColorAt(0, QColor(255, 210, 120, 180));
            g2.setColorAt(1, QColor(255, 120, 60, 0));
            q.fillRect(leak.rect(), g2);
        }
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        p.setOpacity(k);
        p.drawImage(0, 0, leak);
    } else if (id == "rgb-split") {
        rgbShift(img, std::max(1, int(k * 0.012 * w)));
    } else if (id == "glitch") {
        const quint32 tick = quint32(qint64(std::floor(t * sp * 10)));
        const quint32 hsh = hash32(tick * 2246822519u + 7);
        const bool burst = (hsh & 1023) / 1023.0 < 0.35 + 0.5 * k;
        rgbShift(img, int((burst ? 1.0 : 0.25) * k * 0.015 * w), burst ? int(k * 0.004 * h) : 0);
        if (burst) {
            const QImage src = img.copy();
            const int slices = 2 + int(k * 7);
            for (int s = 0; s < slices; ++s) {
                const quint32 r = hash32(hsh ^ quint32(s * 7919));
                const int y0 = int((r & 0xffff) / 65535.0 * h);
                const int sh = std::max(2, int(((r >> 16) & 255) / 255.0 * h * 0.07));
                const int off = int(((int((r >> 8) & 255) - 128) / 128.0) * k * 0.08 * w);
                QPainter p(&img);
                p.setClipRect(0, y0, w, sh);
                p.drawImage(off, 0, src);
            }
        }
    } else if (id == "vhs") {
        rgbShift(img, std::max(1, int(0.004 * w * (0.5 + k))));
        tint(img, 0.25 * k, 1.02, 1.0, 0.95);
        addNoise(img, 26 * k, seed, false);
        const int line = std::max(2, h / 240);
        perPixel(img, [&](int &r, int &g, int &b, int, int y) {
            if ((y / line) % 2) {
                r = int(r * (1 - 0.12 * k));
                g = int(g * (1 - 0.12 * k));
                b = int(b * (1 - 0.12 * k));
            }
        });
        // a tracking band drifting up the picture
        const double band = 1.0 - std::fmod(t * 0.15 * sp, 1.0);
        const int by = int(band * h), bh = std::max(2, int(h * 0.03));
        const QImage src = img.copy();
        QPainter p(&img);
        p.setClipRect(0, by, w, bh);
        p.drawImage(int(0.02 * w * k), 0, src);
        p.setOpacity(0.25 * k);
        p.fillRect(0, by, w, bh, Qt::white);
    } else if (id == "scanlines") {
        const int line = std::max(2, h / 270);
        perPixel(img, [&](int &r, int &g, int &b, int, int y) {
            if ((y / line) % 2) {
                r = int(r * (1 - 0.35 * k));
                g = int(g * (1 - 0.35 * k));
                b = int(b * (1 - 0.35 * k));
            }
        });
    } else if (id == "grain") {
        addNoise(img, 60 * k, seed);
    } else if (id == "old-film") {
        tint(img, 0.85 * k, 1.07, 0.93, 0.74);
        const double flicker = 1 + smoothNoise(t * 14, 5) * 0.06 * k;
        perPixel(img, [&](int &r, int &g, int &b, int, int) {
            r = int(r * flicker);
            g = int(g * flicker);
            b = int(b * flicker);
        });
        ExtraAdjust a;
        a.vignette = 0.7 * k;
        a.grain = 0.5 * k;
        colorAdjust(img, a, seed);
        // scratches and dust
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        const quint32 tick = quint32(qint64(std::floor(t * 24)));
        for (int s = 0; s < 3; ++s) {
            const quint32 r = hash32(tick * 31 + quint32(s));
            if ((r & 255) > 140 * k)
                continue;
            const double x = (r >> 8 & 0xfff) / 4095.0 * w;
            p.setPen(QPen(QColor(255, 255, 240, 110), std::max(1.0, w / 900.0)));
            p.drawLine(QPointF(x, 0), QPointF(x + ((r >> 20) & 15) - 7, h));
        }
        for (int s = 0; s < int(6 * k); ++s) {
            const quint32 r = hash32(tick * 131 + quint32(s) * 17);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 15, 10, 150));
            p.drawEllipse(QPointF((r & 0xffff) / 65535.0 * w, (r >> 16) / 65535.0 * h), w / 500.0 + (r & 3), w / 600.0 + (r & 1));
        }
    } else if (id == "pixelate") {
        const int block = std::max(2, int(2 + k * 0.05 * w));
        const QImage small = img.scaled(std::max(1, w / block), std::max(1, h / block), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        img = small.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else if (id == "mirror") {
        const QImage left = img.copy(0, 0, w / 2, h).mirrored(true, false);
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(w - left.width(), 0, left);
    } else if (id == "kaleidoscope") {
        const QImage q = img.copy(0, 0, (w + 1) / 2, (h + 1) / 2);
        QPainter p(&img);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(w / 2, 0, q.mirrored(true, false));
        p.drawImage(0, h / 2, q.mirrored(false, true));
        p.drawImage(w / 2, h / 2, q.mirrored(true, true));
    }
    Q_UNUSED(prog);
}

// ------------------------------------------------------------------ blend modes

QStringList blendModes()
{
    return {"normal", "multiply", "screen", "overlay", "darken", "lighten", "color-dodge", "color-burn", "hard-light",
            "soft-light", "difference", "exclusion", "add"};
}

QPainter::CompositionMode compositionMode(const QString &b)
{
    if (b == "multiply") return QPainter::CompositionMode_Multiply;
    if (b == "screen") return QPainter::CompositionMode_Screen;
    if (b == "overlay") return QPainter::CompositionMode_Overlay;
    if (b == "darken") return QPainter::CompositionMode_Darken;
    if (b == "lighten") return QPainter::CompositionMode_Lighten;
    if (b == "color-dodge") return QPainter::CompositionMode_ColorDodge;
    if (b == "color-burn") return QPainter::CompositionMode_ColorBurn;
    if (b == "hard-light") return QPainter::CompositionMode_HardLight;
    if (b == "soft-light") return QPainter::CompositionMode_SoftLight;
    if (b == "difference") return QPainter::CompositionMode_Difference;
    if (b == "exclusion") return QPainter::CompositionMode_Exclusion;
    if (b == "add") return QPainter::CompositionMode_Plus;
    return QPainter::CompositionMode_SourceOver;
}

// ------------------------------------------------------------------ transitions

QVector<Info> transitions()
{
    return {
        {"dissolve", "Dissolve", "Basic"},
        {"dip-black", "Dip to black", "Basic"},
        {"dip-white", "Dip to white", "Basic"},
        {"flash", "Flash", "Basic"},
        {"blur", "Blur", "Basic"},
        {"slide-left", "Slide left", "Slide"},
        {"slide-right", "Slide right", "Slide"},
        {"slide-up", "Slide up", "Slide"},
        {"slide-down", "Slide down", "Slide"},
        {"push-left", "Push left", "Slide"},
        {"push-right", "Push right", "Slide"},
        {"push-up", "Push up", "Slide"},
        {"push-down", "Push down", "Slide"},
        {"wipe-left", "Wipe left", "Wipe"},
        {"wipe-right", "Wipe right", "Wipe"},
        {"wipe-up", "Wipe up", "Wipe"},
        {"wipe-down", "Wipe down", "Wipe"},
        {"split-open", "Split open", "Wipe"},
        {"circle-open", "Circle open", "Wipe"},
        {"circle-close", "Circle close", "Wipe"},
        {"zoom-in", "Zoom in", "Camera"},
        {"zoom-out", "Zoom out", "Camera"},
        {"spin", "Spin", "Camera"},
        {"glitch", "Glitch", "Camera"},
    };
}

QString transitionLabel(const QString &id)
{
    for (const Info &i : transitions())
        if (i.id == id)
            return i.label;
    return id;
}

void drawTransition(QPainter &p, const QImage &a, const QImage &b, const QString &type, double progress, quint32 seed)
{
    const double t = clamp01(progress);
    const double e = easeInOut(t);
    const double W = std::max(a.width(), b.width()), H = std::max(a.height(), b.height());
    // a*(1-k) + b*k, correct where either layer is partly transparent
    auto mixed = [&](const QImage &x, const QImage &y, double k) {
        QImage tmp(int(W), int(H), QImage::Format_ARGB32_Premultiplied);
        tmp.fill(Qt::transparent);
        QPainter q(&tmp);
        q.setOpacity(1 - k);
        q.drawImage(0, 0, x);
        q.setCompositionMode(QPainter::CompositionMode_Plus);
        q.setOpacity(k);
        q.drawImage(0, 0, y);
        q.end();
        return tmp;
    };
    auto dissolve = [&](const QImage &x, const QImage &y, double k) { p.drawImage(0, 0, mixed(x, y, k)); };
    // a colour wash limited to where either layer has pixels (does not cover lower tracks)
    auto wash = [&](const QColor &c, double alpha) {
        if (alpha <= 0.001)
            return;
        QImage area(int(W), int(H), QImage::Format_ARGB32_Premultiplied);
        area.fill(Qt::transparent);
        QPainter q(&area);
        q.drawImage(0, 0, a);
        q.drawImage(0, 0, b);
        q.setCompositionMode(QPainter::CompositionMode_SourceIn);
        q.fillRect(area.rect(), c);
        q.end();
        p.save();
        p.setOpacity(p.opacity() * clamp01(alpha));
        p.drawImage(0, 0, area);
        p.restore();
    };
    if (type == "dip-black" || type == "dip-white") {
        p.drawImage(0, 0, t < 0.5 ? a : b);
        wash(type == "dip-black" ? Qt::black : Qt::white, 1 - std::abs(t - 0.5) * 2);
    } else if (type == "flash") {
        dissolve(a, b, e);
        wash(Qt::white, std::pow(clamp01(1 - std::abs(t - 0.5) * 2), 1.6));
    } else if (type == "blur") {
        const double r = (1 - std::abs(2 * t - 1)) * 0.035 * W;
        dissolve(blurred(a, r), blurred(b, r), e);
    } else if (type.startsWith("slide-")) {
        p.drawImage(0, 0, a);
        QPointF off;
        if (type == "slide-left") off = {W * (1 - e), 0};
        else if (type == "slide-right") off = {-W * (1 - e), 0};
        else if (type == "slide-up") off = {0, H * (1 - e)};
        else off = {0, -H * (1 - e)};
        p.drawImage(off, b);
    } else if (type.startsWith("push-")) {
        QPointF dir;
        if (type == "push-left") dir = {-W, 0};
        else if (type == "push-right") dir = {W, 0};
        else if (type == "push-up") dir = {0, -H};
        else dir = {0, H};
        p.drawImage(dir * e, a);
        p.drawImage(dir * (e - 1), b);
    } else if (type.startsWith("wipe-") || type == "split-open") {
        p.drawImage(0, 0, a);
        QRectF r;
        if (type == "wipe-left") r = QRectF(W * (1 - e), 0, W * e, H);
        else if (type == "wipe-right") r = QRectF(0, 0, W * e, H);
        else if (type == "wipe-up") r = QRectF(0, H * (1 - e), W, H * e);
        else if (type == "wipe-down") r = QRectF(0, 0, W, H * e);
        else r = QRectF(W / 2 * (1 - e), 0, W * e, H);
        p.save();
        p.setClipRect(r);
        p.drawImage(0, 0, b);
        p.restore();
    } else if (type == "circle-open" || type == "circle-close") {
        const double diag = std::hypot(W, H) / 2 * 1.02;
        const bool open = type == "circle-open";
        p.drawImage(0, 0, open ? a : b);
        QPainterPath path;
        const double r = diag * (open ? e : 1 - e);
        path.addEllipse(QPointF(W / 2, H / 2), r, r);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setClipPath(path);
        p.drawImage(0, 0, open ? b : a);
        p.restore();
    } else if (type == "zoom-in") {
        drawXform(p, b, 0.8 + 0.2 * e, 0, {}, e);
        drawXform(p, a, 1 + 0.6 * e, 0, {}, 1 - e);
    } else if (type == "zoom-out") {
        drawXform(p, b, 1.35 - 0.35 * e, 0, {}, e);
        drawXform(p, a, 1 - 0.3 * e, 0, {}, 1 - e);
    } else if (type == "spin") {
        if (t < 0.5)
            drawXform(p, a, 1 - e * 0.9, 360 * e, {}, 1);
        else
            drawXform(p, b, 0.1 + 0.9 * e, -360 * (1 - e), {}, 1);
    } else if (type == "glitch") {
        QImage tmp = mixed(a, b, t < 0.5 ? std::pow(t * 2, 3) * 0.5 : 0.5 + 0.5 * (1 - std::pow((1 - t) * 2, 3)));
        EffectSpec g;
        g.type = "glitch";
        g.intensity = 1 - std::abs(2 * t - 1);
        g.speed = 3;
        applyEffect(tmp, g, t * 10, t, seed);
        p.drawImage(0, 0, tmp);
    } else { // dissolve
        dissolve(a, b, e);
    }
}

// ------------------------------------------------------------------ looks

QVector<Look> looks()
{
    auto L = [](QString id, QString label, double ex, double co, double sa, double te, double ti, ExtraAdjust x = {}) {
        Look l;
        l.id = id;
        l.label = label;
        l.color.exposure = ex;
        l.color.contrast = co;
        l.color.saturation = sa;
        l.color.temperature = te;
        l.color.tint = ti;
        l.adjust = x;
        return l;
    };
    auto X = [](double hue, double vib, double hi, double sh, double fade, double vig, double grain) {
        ExtraAdjust a;
        a.hue = hue;
        a.vibrance = vib;
        a.highlights = hi;
        a.shadows = sh;
        a.fade = fade;
        a.vignette = vig;
        a.grain = grain;
        return a;
    };
    return {
        L("none", "Original", 0, 1, 1, 0, 0),
        L("natural", "Natural", 0.05, 1.05, 1.08, 0.05, 0, X(0, 0.15, -0.1, 0.1, 0, 0, 0)),
        L("vivid", "Vivid", 0.05, 1.15, 1.35, 0.05, 0, X(0, 0.3, 0, 0, 0, 0, 0)),
        L("warm", "Warm", 0.05, 1.05, 1.1, 0.45, 0.05),
        L("cool", "Cool", 0, 1.05, 0.95, -0.45, 0),
        L("golden-hour", "Golden hour", 0.1, 1.08, 1.15, 0.6, 0.08, X(-6, 0.2, -0.15, 0.15, 0.1, 0.25, 0)),
        L("cinematic", "Cinematic", -0.05, 1.18, 0.9, -0.12, -0.05, X(0, 0.1, -0.2, 0.05, 0.1, 0.35, 0)),
        L("teal-orange", "Teal & orange", 0, 1.15, 1.15, 0.25, -0.12, X(-8, 0.25, -0.1, 0, 0.05, 0.2, 0)),
        L("moody", "Moody", -0.25, 1.15, 0.75, -0.1, 0, X(0, -0.1, -0.25, -0.15, 0.15, 0.5, 0)),
        L("faded", "Faded film", 0.15, 0.8, 0.7, 0.15, 0, X(0, 0, -0.1, 0.1, 0.55, 0.15, 0.15)),
        L("vintage", "Vintage", 0.05, 0.9, 0.75, 0.35, 0.05, X(-10, 0, -0.1, 0.1, 0.45, 0.4, 0.3)),
        L("matte", "Matte", 0.05, 0.88, 0.95, 0, 0, X(0, 0, -0.1, 0.1, 0.6, 0, 0)),
        L("pastel", "Pastel", 0.2, 0.8, 0.85, 0.05, 0.08, X(0, -0.2, -0.2, 0.25, 0.35, 0, 0)),
        L("bright-airy", "Bright & airy", 0.35, 0.92, 1.05, -0.05, 0, X(0, 0.1, -0.2, 0.35, 0.15, 0, 0)),
        L("dramatic", "Dramatic", -0.1, 1.4, 1.05, 0, 0, X(0, 0.15, -0.3, -0.2, 0, 0.45, 0)),
        L("noir", "Noir", -0.05, 1.45, 0, 0, 0, X(0, 0, -0.2, -0.25, 0, 0.5, 0.2)),
        L("bw", "Black & white", 0, 1.15, 0, 0, 0),
        L("sepia", "Sepia", 0.05, 1.05, 0.25, 0.8, 0.1, X(0, 0, 0, 0, 0.2, 0.3, 0.15)),
        L("arctic", "Arctic", 0.1, 1.05, 0.7, -0.7, 0, X(0, 0, -0.1, 0.1, 0.1, 0, 0)),
        L("sunset", "Sunset", 0.05, 1.1, 1.25, 0.55, 0.3, X(-12, 0.2, 0, 0, 0.05, 0.25, 0)),
        L("retro-80s", "Retro 80s", 0.05, 1.15, 1.3, 0.1, 0.35, X(18, 0.35, 0, 0, 0.2, 0.2, 0.2)),
        L("neon-nights", "Neon nights", -0.1, 1.25, 1.4, -0.35, 0.45, X(25, 0.4, 0.1, -0.1, 0, 0.35, 0)),
        L("polaroid", "Instant photo", 0.15, 0.92, 0.85, 0.25, 0.05, X(-4, 0, -0.2, 0.2, 0.4, 0.35, 0.1)),
        L("green-forest", "Forest", 0, 1.1, 1.1, -0.1, -0.25, X(10, 0.2, -0.1, 0.1, 0.05, 0.2, 0)),
    };
}

} // namespace ImageFx
} // namespace ss
