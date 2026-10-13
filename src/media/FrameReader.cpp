#include "FrameReader.h"
#include "../core/Tools.h"
#include "ExportGraph.h"

#include <QImageReader>
#include <QProcess>
#include <algorithm>
#include <cmath>

namespace ss {

FrameReader::FrameReader(const Project &p, QSize outputSize) : m_project(p), m_out(outputSize) {}

FrameReader::~FrameReader()
{
    for (Decoder *d : std::as_const(m_decoders))
        close(d);
    m_decoders.clear();
}

void FrameReader::close(Decoder *d)
{
    if (!d)
        return;
    if (d->proc) {
        d->proc->kill();
        d->proc->waitForFinished(3000);
        delete d->proc;
    }
    delete d;
}

void FrameReader::retain(const QSet<QString> &clipIds)
{
    for (auto it = m_decoders.begin(); it != m_decoders.end();) {
        if (!clipIds.contains(it.key())) {
            close(it.value());
            it = m_decoders.erase(it);
        } else {
            ++it;
        }
    }
}

QSize FrameReader::decodeSize(const Clip &clip, const MediaAsset &m) const
{
    // Decode no larger than the picture will be drawn (including fill/crop and the largest
    // keyframed zoom), never larger than the source.
    Transform t = clip.transform;
    for (const Keyframe &k : clip.fx.keyframes)
        t.scale = std::max(t.scale, k.scale);
    t.scale = std::min(t.scale, 4.0);
    t.rotation = 0;
    const ClipGeometry g = computeGeometry(m.width, m.height, t, m_out.width(), m_out.height());
    const double f = std::clamp(std::max(g.scaleX, g.scaleY) * 1.05, 0.05, 1.0);
    QSize s(std::max(2, int(std::lround(m.width * f))), std::max(2, int(std::lround(m.height * f))));
    s.setWidth(std::max(2, s.width() & ~1));
    s.setHeight(std::max(2, s.height() & ~1));
    return s;
}

FrameReader::Decoder *FrameReader::open(const Clip &clip, const MediaAsset &m, TimeUs startSrc)
{
    auto *d = new Decoder;
    d->startSrc = startSrc;
    d->size = decodeSize(clip, m);
    // Decode at the source's own frame rate: every original frame is available for slow
    // motion, and fast clips simply skip frames while reading forwards.
    const double srcFps = m.fps.isValid() ? m.fps.toDouble() : m_project.canvas.fps.toDouble();
    d->rate = std::clamp(srcFps > 0 ? srcFps : 30.0, 1.0, 120.0);
    const QString ffmpeg = Tools::ffmpeg();
    if (ffmpeg.isEmpty()) {
        m_error = "FFmpeg was not found.";
        d->eof = true;
        return d;
    }
    d->proc = new QProcess;
    d->proc->setProcessChannelMode(QProcess::SeparateChannels);
    d->proc->setStandardErrorFile(QProcess::nullDevice());
    const QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-ss", QString::number(double(startSrc) / kUsPerSec, 'f', 6),
                           "-i", m.path, "-an", "-sn", "-dn", "-vf",
                           QString("fps=%1,scale=%2:%3:flags=bicubic,format=rgba").arg(d->rate, 0, 'f', 4).arg(d->size.width()).arg(d->size.height()),
                           "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"};
    d->proc->start(ffmpeg, args);
    if (!d->proc->waitForStarted(10000)) {
        m_error = "Could not start FFmpeg to decode " + m.name;
        d->eof = true;
    }
    return d;
}

QImage FrameReader::frame(const Clip &clip, const MediaAsset &m, TimeUs sourceTime)
{
    if (m.kind == MediaKind::Image) {
        auto it = m_stills.find(m.id);
        if (it == m_stills.end()) {
            QImageReader r(m.path);
            r.setAutoTransform(true);
            QImage img = r.read();
            const int box = std::max(m_out.width(), m_out.height()) * 2;
            if (!img.isNull() && (img.width() > box || img.height() > box))
                img = img.scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            it = m_stills.insert(m.id, img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        }
        return it.value();
    }
    Decoder *d = m_decoders.value(clip.id);
    if (!d) {
        d = open(clip, m, sourceTime);
        m_decoders.insert(clip.id, d);
    }
    const qint64 want = std::max<qint64>(0, qint64(std::floor(double(sourceTime - d->startSrc) * d->rate / kUsPerSec + 1e-6)));
    const qsizetype bytes = qsizetype(d->size.width()) * d->size.height() * 4;
    while (!d->eof && d->index < want && d->proc) {
        while (d->proc->bytesAvailable() < bytes) {
            if (!d->proc->waitForReadyRead(30000)) {
                if (d->proc->bytesAvailable() < bytes) {
                    d->eof = true;
                    break;
                }
            }
        }
        if (d->eof)
            break;
        const QByteArray data = d->proc->read(bytes);
        if (data.size() < bytes) {
            d->eof = true;
            break;
        }
        ++d->index;
        if (d->index == want || want - d->index < 1) {
            QImage img(reinterpret_cast<const uchar *>(data.constData()), d->size.width(), d->size.height(),
                       d->size.width() * 4, QImage::Format_RGBA8888);
            d->last = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }
    return d->last;
}

} // namespace ss
