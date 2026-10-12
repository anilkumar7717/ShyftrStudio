#pragma once

#include "../core/Model.h"

#include <QImage>
#include <array>

namespace ss {

// One definition of the colour adjustments, used both by the preview (CPU on QImage)
// and the export graph (FFmpeg lutrgb + colorchannelmixer), so both match.
//   1. exposure + contrast as a per-channel curve:  v' = clamp(((v*2^ev) - 0.5) * contrast + 0.5)
//   2. white balance gains and saturation as a 3x3 matrix (Rec.709 luma weights)
namespace ColorMath {
std::array<quint8, 256> curveLut(const ColorAdjust &c);
std::array<double, 9> matrix(const ColorAdjust &c);
void apply(QImage &img, const ColorAdjust &c); // in place, ARGB32/RGB32 formats
QString ffmpegFilter(const ColorAdjust &c);    // empty when identity
} // namespace ColorMath

} // namespace ss
