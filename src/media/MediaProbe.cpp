#include "MediaProbe.h"
#include "../core/Tools.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdlib>

namespace ss {
namespace MediaProbe {

bool isImageFile(const QString &path)
{
    static const QStringList exts{"png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QStringList importFilters()
{
    return {"Media (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.mts *.m2ts *.mp3 *.m4a *.wav *.aac *.flac *.ogg *.opus "
            "*.aif *.aiff *.png *.jpg *.jpeg *.webp *.bmp)",
            "Video (*.mp4 *.mov *.m4v *.mkv *.webm *.avi *.mts *.m2ts)",
            "Audio (*.mp3 *.m4a *.wav *.aac *.flac *.ogg *.opus *.aif *.aiff)",
            "Images (*.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"};
}

static int rotationFromStream(const QJsonObject &s)
{
    // Newer ffprobe: side_data_list[].rotation; older: tags.rotate
    for (const QJsonValue &v : s.value("side_data_list").toArray()) {
        const QJsonObject sd = v.toObject();
        if (sd.contains("rotation")) {
            int r = int(std::lround(sd.value("rotation").toDouble()));
            r = ((r % 360) + 360) % 360;
            return r;
        }
    }
    const QString tag = s.value("tags").toObject().value("rotate").toString();
    if (!tag.isEmpty()) {
        int r = tag.toInt();
        // tags.rotate is clockwise; side data is counter-clockwise. Normalise to "needs swap" semantics.
        return ((360 - r) % 360 + 360) % 360;
    }
    return 0;
}

bool parseProbeJson(const QByteArray &json, MediaAsset &m, QString *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (error)
            *error = "ffprobe returned no information for this file.";
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject fmt = root.value("format").toObject();
    double duration = fmt.value("duration").toString().toDouble();
    m.hasVideo = m.hasAudio = false;
    bool attachedPicOnly = true;
    for (const QJsonValue &v : root.value("streams").toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s.value("codec_type").toString();
        if (type == "video") {
            const bool attached = s.value("disposition").toObject().value("attached_pic").toInt() == 1;
            if (attached || m.hasVideo)
                continue;
            attachedPicOnly = false;
            m.hasVideo = true;
            m.videoCodec = s.value("codec_name").toString();
            m.width = s.value("width").toInt();
            m.height = s.value("height").toInt();
            const Rational r = Rational::fromString(s.value("r_frame_rate").toString(), {0, 1});
            const Rational a = Rational::fromString(s.value("avg_frame_rate").toString(), {0, 1});
            Rational use = a.isValid() ? a : r;
            if (!use.isValid() || use.toDouble() > 240)
                use = r.isValid() && r.toDouble() <= 240 ? r : Rational{30, 1};
            m.fps = Rational::fromDouble(use.toDouble());
            m.variableFrameRate = a.isValid() && r.isValid() && std::abs(a.toDouble() - r.toDouble()) > 0.01;
            m.rotation = rotationFromStream(s);
            if (m.rotation == 90 || m.rotation == 270)
                std::swap(m.width, m.height); // ffmpeg auto-rotates on decode
            const double sd = s.value("duration").toString().toDouble();
            if (duration <= 0 && sd > 0)
                duration = sd;
        } else if (type == "audio" && !m.hasAudio) {
            m.hasAudio = true;
            m.audioCodec = s.value("codec_name").toString();
            m.audioChannels = s.value("channels").toInt();
            m.sampleRate = s.value("sample_rate").toString().toInt();
            const double sd = s.value("duration").toString().toDouble();
            if (duration <= 0 && sd > 0)
                duration = sd;
        }
    }
    if (!m.hasVideo && !m.hasAudio) {
        if (error)
            *error = "The file has no playable audio or video streams.";
        return false;
    }
    if (m.hasVideo && !m.hasAudio && (duration <= 0 || fmt.value("format_name").toString().contains("image2") ||
                                      fmt.value("format_name").toString().endsWith("_pipe")))
        m.kind = MediaKind::Image;
    else if (!m.hasVideo || attachedPicOnly)
        m.kind = MediaKind::Audio;
    else
        m.kind = MediaKind::Video;
    if (m.kind == MediaKind::Audio)
        m.hasVideo = false;
    m.duration = m.kind == MediaKind::Image ? 0 : TimeUs(std::llround(duration * kUsPerSec));
    if (m.kind != MediaKind::Image && m.duration <= 0) {
        if (error)
            *error = "Could not determine the media duration.";
        return false;
    }
    return true;
}

bool probe(const QString &path, MediaAsset &m, QString *error)
{
    QFileInfo fi(path);
    if (!fi.isFile()) {
        if (error)
            *error = "File not found: " + path;
        return false;
    }
    if (m.name.isEmpty())
        m.name = fi.fileName();
    m.path = fi.absoluteFilePath();
    m.fileSize = fi.size();

    if (isImageFile(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize sz = reader.size();
        if (sz.isValid()) {
            m.kind = MediaKind::Image;
            m.hasVideo = true;
            m.hasAudio = false;
            m.width = sz.width();
            m.height = sz.height();
            m.duration = 0;
            return true;
        }
    }
    const QString ffprobe = Tools::ffprobe();
    if (ffprobe.isEmpty()) {
        if (error)
            *error = "ffprobe was not found. Install FFmpeg or set its location in Preferences → Tools.";
        return false;
    }
    const auto r = Tools::run(ffprobe, {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path}, 30000);
    if (!r.started || r.exitCode != 0) {
        if (error)
            *error = "This file could not be read: " + QString::fromUtf8(r.err).trimmed().left(300);
        return false;
    }
    return parseProbeJson(r.out, m, error);
}

} // namespace MediaProbe
} // namespace ss
