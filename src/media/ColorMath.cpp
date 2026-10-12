#include "ColorMath.h"

#include <cmath>
#include <algorithm>

namespace ss {
namespace ColorMath {

static double curve(double v01, const ColorAdjust &c)
{
    const double e = std::pow(2.0, std::clamp(c.exposure, -4.0, 4.0));
    const double v = (v01 * e - 0.5) * std::clamp(c.contrast, 0.0, 3.0) + 0.5;
    return std::clamp(v, 0.0, 1.0);
}

std::array<quint8, 256> curveLut(const ColorAdjust &c)
{
    std::array<quint8, 256> lut{};
    for (int i = 0; i < 256; ++i)
        lut[i] = quint8(std::lround(curve(i / 255.0, c) * 255.0));
    return lut;
}

std::array<double, 9> matrix(const ColorAdjust &c)
{
    const double t = std::clamp(c.temperature, -1.0, 1.0);
    const double m = std::clamp(c.tint, -1.0, 1.0);
    const double gr = 1.0 + 0.25 * t, gg = 1.0 - 0.18 * m, gb = 1.0 - 0.25 * t;
    const double s = std::clamp(c.saturation, 0.0, 3.0);
    const double wr = 0.2126, wg = 0.7152, wb = 0.0722;
    // saturation matrix S
    const double S[9] = {wr + s * (1 - wr), wg * (1 - s),      wb * (1 - s),
                         wr * (1 - s),      wg + s * (1 - wg), wb * (1 - s),
                         wr * (1 - s),      wg * (1 - s),      wb + s * (1 - wb)};
    const double G[3] = {gr, gg, gb};
    std::array<double, 9> M{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            M[row * 3 + col] = S[row * 3 + col] * G[col];
    return M;
}

void apply(QImage &img, const ColorAdjust &c)
{
    if (c.isIdentity() || img.isNull())
        return;
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);
    const auto lut = curveLut(c);
    const auto M = matrix(c);
    const bool premul = img.format() == QImage::Format_ARGB32_Premultiplied;
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb px = premul ? qUnpremultiply(line[x]) : line[x];
            const double r = lut[qRed(px)], g = lut[qGreen(px)], b = lut[qBlue(px)];
            const int nr = std::clamp(int(std::lround(M[0] * r + M[1] * g + M[2] * b)), 0, 255);
            const int ng = std::clamp(int(std::lround(M[3] * r + M[4] * g + M[5] * b)), 0, 255);
            const int nb = std::clamp(int(std::lround(M[6] * r + M[7] * g + M[8] * b)), 0, 255);
            px = qRgba(nr, ng, nb, qAlpha(px));
            line[x] = premul ? qPremultiply(px) : px;
        }
    }
}

QString ffmpegFilter(const ColorAdjust &c)
{
    if (c.isIdentity())
        return {};
    QStringList parts;
    if (c.exposure != 0 || c.contrast != 1) {
        const double e = std::pow(2.0, std::clamp(c.exposure, -4.0, 4.0));
        const double k = std::clamp(c.contrast, 0.0, 3.0);
        const QString expr = QString("clip((val*%1-127.5)*%2+127.5,0,255)")
                                 .arg(QString::number(e, 'f', 6), QString::number(k, 'f', 6));
        parts << QString("lutrgb=r='%1':g='%1':b='%1'").arg(expr);
    }
    if (c.saturation != 1 || c.temperature != 0 || c.tint != 0) {
        const auto M = matrix(c);
        auto f = [](double v) { return QString::number(v, 'f', 6); };
        parts << QString("colorchannelmixer=rr=%1:rg=%2:rb=%3:gr=%4:gg=%5:gb=%6:br=%7:bg=%8:bb=%9")
                     .arg(f(M[0]), f(M[1]), f(M[2]), f(M[3]), f(M[4]), f(M[5]), f(M[6]), f(M[7]), f(M[8]));
    }
    return parts.join(',');
}

} // namespace ColorMath
} // namespace ss
