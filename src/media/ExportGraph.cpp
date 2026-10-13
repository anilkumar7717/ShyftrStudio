#include "ExportGraph.h"
#include "ColorMath.h"

#include <QFileInfo>
#include <QtMath>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace ss {

// ------------------------------------------------------------------ settings

QJsonObject ExportSettings::toJson() const
{
    QJsonObject o;
    o["presetId"] = presetId;
    o["width"] = width;
    o["height"] = height;
    o["fps"] = fps.toString();
    o["videoCodec"] = videoCodec;
    o["encoder"] = encoder;
    o["rateMode"] = rateMode;
    o["crf"] = crf;
    o["videoBitrateKbps"] = videoBitrateKbps;
    o["audioBitrateKbps"] = audioBitrateKbps;
    o["speedPreset"] = speedPreset;
    o["burnCaptions"] = burnCaptions;
    o["writeSrt"] = writeSrt;
    o["writeVtt"] = writeVtt;
    o["normalizeLoudness"] = normalizeLoudness;
    o["lastOutputDir"] = QFileInfo(outputPath).absolutePath();
    return o;
}

ExportSettings ExportSettings::fromJson(const QJsonObject &o)
{
    ExportSettings s;
    s.presetId = o.value("presetId").toString(s.presetId);
    s.width = o.value("width").toInt(s.width);
    s.height = o.value("height").toInt(s.height);
    s.fps = Rational::fromString(o.value("fps").toString(), s.fps);
    s.videoCodec = o.value("videoCodec").toString(s.videoCodec);
    s.encoder = o.value("encoder").toString(s.encoder);
    s.rateMode = o.value("rateMode").toString(s.rateMode);
    s.crf = o.value("crf").toInt(s.crf);
    s.videoBitrateKbps = o.value("videoBitrateKbps").toInt(s.videoBitrateKbps);
    s.audioBitrateKbps = o.value("audioBitrateKbps").toInt(s.audioBitrateKbps);
    s.speedPreset = o.value("speedPreset").toString(s.speedPreset);
    s.burnCaptions = o.value("burnCaptions").toBool(true);
    s.writeSrt = o.value("writeSrt").toBool(false);
    s.writeVtt = o.value("writeVtt").toBool(false);
    s.normalizeLoudness = o.value("normalizeLoudness").toBool(false);
    return s;
}

QVector<ExportPreset> exportPresets()
{
    return {
        {"youtube-1080p", "1080p HD · 16:9", 1920, 1080, 30, 1, "h264", 20, 12000},
        {"youtube-4k", "4K UHD · 16:9", 3840, 2160, 30, 1, "h264", 20, 40000},
        {"hd-720p", "720p · 16:9", 1280, 720, 30, 1, "h264", 21, 6000},
        {"shorts-1080", "Shorts / Reels · 9:16 1080×1920", 1080, 1920, 30, 1, "h264", 20, 12000},
        {"shorts-720", "Vertical 720×1280 · 9:16", 720, 1280, 30, 1, "h264", 21, 6000},
        {"square-1080", "Square 1080×1080 · 1:1", 1080, 1080, 30, 1, "h264", 20, 10000},
        {"portrait-1080", "Portrait 1080×1350 · 4:5", 1080, 1350, 30, 1, "h264", 20, 10000},
        {"master-4k-hevc", "4K HEVC master · 16:9", 3840, 2160, 30, 1, "hevc", 20, 30000},
    };
}

// ------------------------------------------------------------------ geometry

ClipGeometry computeGeometry(int srcW, int srcH, const Transform &t, int canvasW, int canvasH)
{
    ClipGeometry g;
    srcW = std::max(1, srcW);
    srcH = std::max(1, srcH);
    const double l = std::clamp(t.cropL, 0.0, 0.95), r = std::clamp(t.cropR, 0.0, 0.95);
    const double tp = std::clamp(t.cropT, 0.0, 0.95), b = std::clamp(t.cropB, 0.0, 0.95);
    const double cw = std::max(2.0, srcW * (1 - l - r));
    const double ch = std::max(2.0, srcH * (1 - tp - b));
    g.crop = QRectF(std::round(srcW * l), std::round(srcH * tp), std::round(cw), std::round(ch));

    double angle = std::fmod(t.rotation, 360.0);
    if (angle < 0)
        angle += 360;
    const int quarters = int(std::lround(angle / 90.0)) % 4;
    if (std::abs(angle - quarters * 90.0) < 0.01 || std::abs(angle - 360.0) < 0.01) {
        g.quarterTurns = quarters;
        g.freeAngle = 0;
    } else {
        g.quarterTurns = 0;
        g.freeAngle = angle;
    }
    double w = g.crop.width(), h = g.crop.height();
    if (g.quarterTurns % 2 == 1)
        std::swap(w, h);
    if (g.freeAngle != 0) {
        const double rad = qDegreesToRadians(g.freeAngle);
        const double bw = std::abs(w * std::cos(rad)) + std::abs(h * std::sin(rad));
        const double bh = std::abs(w * std::sin(rad)) + std::abs(h * std::cos(rad));
        w = bw;
        h = bh;
    }
    g.boxW = w;
    g.boxH = h;
    const double userScale = std::clamp(t.scale, 0.01, 20.0);
    if (t.fit == "stretch") {
        g.scaleX = canvasW / w * userScale;
        g.scaleY = canvasH / h * userScale;
        g.scale = g.scaleX;
    } else {
        const double s = t.fit == "fill" ? std::max(canvasW / w, canvasH / h) : std::min(canvasW / w, canvasH / h);
        g.scale = s * userScale;
        g.scaleX = g.scaleY = g.scale;
    }
    g.outW = std::max(2.0, std::round(w * g.scaleX / 2) * 2);
    g.outH = std::max(2.0, std::round(h * g.scaleY / 2) * 2);
    g.x = std::round((canvasW - g.outW) / 2 + t.posX * canvasW);
    g.y = std::round((canvasH - g.outH) / 2 + t.posY * canvasH);
    return g;
}

// ------------------------------------------------------------------ helpers

QStringList ExportGraph::atempoChain(double speed)
{
    QStringList out;
    if (std::abs(speed - 1.0) < 1e-6)
        return out;
    double s = speed;
    while (s > 2.0) {
        out << "atempo=2.0";
        s /= 2.0;
    }
    while (s < 0.5) {
        out << "atempo=0.5";
        s /= 0.5;
    }
    out << QString("atempo=%1").arg(s, 0, 'f', 6);
    return out;
}

int ExportGraph::estimateBitrateKbps(int w, int h, Rational fps, int crf)
{
    // bits-per-pixel heuristic: ~0.1 bpp at crf 20 for 30 fps, halves every +6 crf
    const double bpp = 0.1 * std::pow(0.5, (crf - 20) / 6.0);
    const double kbps = w * double(h) * std::max(1.0, fps.toDouble()) * bpp / 1000.0;
    return int(std::clamp(kbps, 500.0, 150000.0));
}

QString ExportGraph::chooseEncoder(const QString &codec, const QString &pref, const QStringList &avail, QStringList *warnings)
{
    const bool hevc = codec == "hevc";
    QStringList hardware;
#if defined(Q_OS_MACOS)
    hardware << (hevc ? "hevc_videotoolbox" : "h264_videotoolbox");
#elif defined(Q_OS_WIN)
    hardware << (hevc ? QStringList{"hevc_nvenc", "hevc_qsv", "hevc_amf"} : QStringList{"h264_nvenc", "h264_qsv", "h264_amf"});
#else
    hardware << (hevc ? QStringList{"hevc_nvenc", "hevc_vaapi"} : QStringList{"h264_nvenc", "h264_vaapi"});
#endif
    const QStringList software = hevc ? QStringList{"libx265"} : QStringList{"libx264", "libopenh264"};
    auto firstAvail = [&](const QStringList &l) -> QString {
        for (const QString &e : l)
            if (avail.contains(e))
                return e;
        return {};
    };
    QString enc;
    if (pref == "hardware" || pref == "auto")
        enc = firstAvail(hardware);
    if (enc.isEmpty())
        enc = firstAvail(software);
    if (enc.isEmpty()) {
        if (warnings)
            *warnings << QString("No %1 encoder was found in this FFmpeg build; falling back to the FFmpeg default.")
                             .arg(hevc ? "HEVC" : "H.264");
        enc = hevc ? "libx265" : "libx264";
    } else if (pref == "hardware" && !hardware.contains(enc) && warnings) {
        *warnings << "Hardware encoding is not available; using the software encoder.";
    }
    return enc;
}

static QString num(double v)
{
    QString s = QString::number(v, 'f', 6);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.')))
        s.chop(1);
    return s;
}

static QString secs(TimeUs t)
{
    return num(double(t) / kUsPerSec);
}

// ------------------------------------------------------------------ build

bool ExportGraph::isAudioOnlyPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == "mp3" || ext == "wav" || ext == "m4a" || ext == "aac" || ext == "flac";
}

ExportPlan ExportGraph::build(const Project &p, const ExportSettings &s, bool needsOverlay, int ffmpegMajor,
                              const QStringList &availableEncoders, const QString &scriptPath, bool compositorVideo)
{
    ExportPlan plan;
    plan.audioOnly = isAudioOnlyPath(s.outputPath);
    if (plan.audioOnly) {
        compositorVideo = false;
        needsOverlay = false;
    }
    plan.compositorVideo = compositorVideo;
    const int W = s.width & ~1, H = s.height & ~1;
    const Rational fps = s.fps.isValid() ? s.fps : p.canvas.fps;
    const QString FPS = fps.toString();
    const int SR = p.canvas.sampleRate > 0 ? p.canvas.sampleRate : 48000;
    const TimeUs R0 = std::max<TimeUs>(0, s.rangeStart);
    const TimeUs R1 = s.rangeEnd > R0 ? std::min(s.rangeEnd, p.duration()) : p.duration();
    if (R1 <= R0) {
        plan.error = "There is nothing to export: the timeline (or the selected range) is empty.";
        return plan;
    }
    plan.duration = R1 - R0;
    const qint64 TOTAL = std::max<qint64>(1, usToFrameRound(plan.duration, fps));
    plan.frameCount = TOTAL;
    const qint64 TOTAL_SAMPLES = qint64(std::llround(double(plan.duration) * SR / kUsPerSec));
    const QString canvasRGBA = QString("color=c=black@0.0:s=%1x%2:r=%3").arg(W).arg(H).arg(FPS);

    QStringList inputs;
    QStringList graph;
    int inputIndex = 0;
    auto addInput = [&](const QStringList &a) {
        inputs << a;
        return inputIndex++;
    };
    auto fpsDur = [&](qint64 frames) { return num(double(frames) * fps.den / fps.num + 1.0); };

    // ---------------- video tracks
    QStringList trackLabels;
    int segCounter = 0;
    for (int ti = 0; ti < p.tracks.size() && !compositorVideo && !plan.audioOnly; ++ti) {
        const Track &tr = p.tracks[ti];
        if (tr.kind != TrackKind::Video || tr.hidden)
            continue;
        QStringList segs;
        qint64 cursor = 0;
        for (const Clip &c : tr.clips) {
            if (!c.enabled || c.type != ClipType::Media)
                continue;
            const MediaAsset *m = p.findMedia(c.mediaId);
            if (!m || m->kind == MediaKind::Audio)
                continue;
            if (m->missing || !QFileInfo::exists(m->path)) {
                plan.warnings << QString("Skipped missing media: %1").arg(m->name);
                continue;
            }
            const TimeUs a = std::max(c.start, R0);
            const TimeUs b = std::min(c.end(), R1);
            if (b <= a)
                continue;
            const qint64 f0 = usToFrameRound(a - R0, fps);
            qint64 f1 = usToFrameRound(b - R0, fps);
            f1 = std::min(f1, TOTAL);
            if (f1 <= f0 || f0 < cursor)
                continue;
            const qint64 nFrames = f1 - f0;
            const TimeUs clipDurUs = framesToUs(nFrames, fps);

            // gap before this clip
            if (f0 > cursor) {
                const QString g = QString("g%1").arg(segCounter++);
                graph << QString("%1:d=%2,format=rgba,trim=end_frame=%3,setpts=PTS-STARTPTS[%4]")
                             .arg(canvasRGBA, fpsDur(f0 - cursor))
                             .arg(f0 - cursor)
                             .arg(g);
                segs << g;
            }

            int idx;
            if (m->kind == MediaKind::Image) {
                idx = addInput({"-loop", "1", "-framerate", FPS, "-t", secs(clipDurUs + kUsPerSec), "-i", m->path});
            } else {
                const TimeUs srcIn = c.timelineToSource(a);
                const TimeUs srcOut = std::min(c.timelineToSource(b), c.out);
                const TimeUs srcDur = std::max<TimeUs>(srcOut - srcIn, frameDurationUs(fps));
                idx = addInput({"-ss", secs(srcIn), "-t", secs(srcDur + 2 * frameDurationUs(fps)), "-i", m->path});
            }

            const ClipGeometry geo = computeGeometry(m->width, m->height, c.transform, W, H);
            QStringList f;
            f << "setpts=PTS-STARTPTS";
            if (m->kind != MediaKind::Image && std::abs(c.speed - 1.0) > 1e-6)
                f << QString("setpts=PTS/%1").arg(num(c.speed));
            f << QString("fps=%1").arg(FPS) << "format=rgba";
            if (geo.crop.x() > 0 || geo.crop.y() > 0 || geo.crop.width() < m->width || geo.crop.height() < m->height)
                f << QString("crop=%1:%2:%3:%4")
                         .arg(int(geo.crop.width()))
                         .arg(int(geo.crop.height()))
                         .arg(int(geo.crop.x()))
                         .arg(int(geo.crop.y()));
            if (c.transform.flipH)
                f << "hflip";
            if (c.transform.flipV)
                f << "vflip";
            if (geo.quarterTurns == 1)
                f << "transpose=1";
            else if (geo.quarterTurns == 2)
                f << "hflip" << "vflip";
            else if (geo.quarterTurns == 3)
                f << "transpose=2";
            if (geo.freeAngle != 0)
                f << QString("rotate=a=%1:ow='rotw(%1)':oh='roth(%1)':c=black@0").arg(num(qDegreesToRadians(geo.freeAngle)));
            const QString color = ColorMath::ffmpegFilter(c.color);
            if (!color.isEmpty())
                f << color;
            f << QString("scale=%1:%2:flags=bicubic").arg(int(geo.outW)).arg(int(geo.outH));
            if (c.transform.opacity < 0.999)
                f << QString("colorchannelmixer=aa=%1").arg(num(std::clamp(c.transform.opacity, 0.0, 1.0)));
            // fades (relative to the visible part of the clip)
            const TimeUs fadeIn = std::max<TimeUs>(0, c.start + c.fadeIn - a);
            const TimeUs fadeOut = std::max<TimeUs>(0, b - (c.end() - c.fadeOut));
            if (c.fadeIn > 0 && fadeIn > 0)
                f << QString("fade=t=in:st=0:d=%1:alpha=1").arg(secs(std::min(fadeIn, clipDurUs)));
            if (c.fadeOut > 0 && fadeOut > 0)
                f << QString("fade=t=out:st=%1:d=%2:alpha=1")
                         .arg(secs(std::max<TimeUs>(0, clipDurUs - fadeOut)), secs(std::min(fadeOut, clipDurUs)));
            f << QString("tpad=stop_mode=clone:stop_duration=%1").arg(secs(clipDurUs + kUsPerSec))
              << QString("trim=end_frame=%1").arg(nFrames) << "setpts=PTS-STARTPTS";
            const QString clipLabel = QString("c%1").arg(segCounter++);
            graph << QString("[%1:v]%2[%3]").arg(idx).arg(f.join(','), clipLabel);
            const QString placed = QString("p%1").arg(segCounter++);
            graph << QString("%1:d=%2,format=rgba,trim=end_frame=%3,setpts=PTS-STARTPTS[cv%4];[cv%4][%5]overlay=x=%6:y=%7:"
                             "format=rgb:eof_action=pass[%8]")
                         .arg(canvasRGBA, fpsDur(nFrames))
                         .arg(nFrames)
                         .arg(placed, clipLabel)
                         .arg(int(geo.x))
                         .arg(int(geo.y))
                         .arg(placed);
            segs << placed;
            cursor = f1;
        }
        if (segs.isEmpty())
            continue;
        if (cursor < TOTAL) {
            const QString g = QString("g%1").arg(segCounter++);
            graph << QString("%1:d=%2,format=rgba,trim=end_frame=%3,setpts=PTS-STARTPTS[%4]")
                         .arg(canvasRGBA, fpsDur(TOTAL - cursor))
                         .arg(TOTAL - cursor)
                         .arg(g);
            segs << g;
        }
        const QString trackLabel = QString("trk%1").arg(ti);
        if (segs.size() == 1) {
            graph << QString("[%1]null[%2]").arg(segs.first(), trackLabel);
        } else {
            QString in;
            for (const QString &sg : segs)
                in += "[" + sg + "]";
            graph << QString("%1concat=n=%2:v=1:a=0[%3]").arg(in).arg(segs.size()).arg(trackLabel);
        }
        trackLabels << trackLabel;
    }

    // ---------------- composite
    if (compositorVideo) {
        plan.usesOverlayPipe = true;
        plan.overlayInputIndex =
            addInput({"-f", "rawvideo", "-pix_fmt", "rgba", "-s", QString("%1x%2").arg(W).arg(H), "-framerate", FPS, "-i", "pipe:0"});
        graph << QString("[%1:v]setpts=PTS-STARTPTS,format=yuv420p,trim=end_frame=%2[vout]").arg(plan.overlayInputIndex).arg(TOTAL);
    } else if (!plan.audioOnly) {
    graph << QString("color=c=%1:s=%2x%3:r=%4:d=%5,format=rgba,trim=end_frame=%6,setpts=PTS-STARTPTS[base]")
                 .arg(p.canvas.background.name(QColor::HexRgb))
                 .arg(W)
                 .arg(H)
                 .arg(FPS, fpsDur(TOTAL))
                 .arg(TOTAL);
    QString current = "base";
    int compCounter = 0;
    for (const QString &tl : trackLabels) {
        const QString out = QString("comp%1").arg(compCounter++);
        graph << QString("[%1][%2]overlay=format=rgb:eof_action=pass[%3]").arg(current, tl, out);
        current = out;
    }
    if (needsOverlay) {
        plan.usesOverlayPipe = true;
        plan.overlayInputIndex =
            addInput({"-f", "rawvideo", "-pix_fmt", "rgba", "-s", QString("%1x%2").arg(W).arg(H), "-framerate", FPS, "-i", "pipe:0"});
        graph << QString("[%1:v]setpts=PTS-STARTPTS[gfx]").arg(plan.overlayInputIndex);
        const QString out = QString("comp%1").arg(compCounter++);
        graph << QString("[%1][gfx]overlay=format=rgb:eof_action=pass[%2]").arg(current, out);
        current = out;
    }
    graph << QString("[%1]format=yuv420p,trim=end_frame=%2[vout]").arg(current).arg(TOTAL);
    }

    // ---------------- audio
    struct AudioPart {
        QString label;
        bool duck = false;
    };
    QVector<AudioPart> parts;
    bool warnedCurve = false;
    for (const Track &tr : p.tracks) {
        if (tr.kind != TrackKind::Audio || tr.muted)
            continue;
        for (const Clip &c : tr.clips) {
            if (!c.enabled || c.audio.mute || c.type != ClipType::Media)
                continue;
            const MediaAsset *m = p.findMedia(c.mediaId);
            if (!m || !m->hasAudio || m->missing || !QFileInfo::exists(m->path))
                continue;
            if (c.fx.speedCurve.isSet()) {
                if (!warnedCurve)
                    plan.warnings << "Audio of clips with a speed curve is muted (the sound cannot follow a curve).";
                warnedCurve = true;
                continue;
            }
            const TimeUs a = std::max(c.start, R0);
            const TimeUs b = std::min(c.end(), R1);
            if (b <= a)
                continue;
            const TimeUs srcIn = c.timelineToSource(a);
            const TimeUs srcOut = std::min(c.timelineToSource(b), c.out);
            const int idx = addInput({"-ss", secs(srcIn), "-t", secs(std::max<TimeUs>(srcOut - srcIn, 1000) + 100 * kUsPerMs), "-i", m->path});
            const qint64 nSamples = qint64(std::llround(double(b - a) * SR / kUsPerSec));
            const qint64 delaySamples = qint64(std::llround(double(a - R0) * SR / kUsPerSec));
            const double gain = std::pow(10.0, (c.audio.gainDb + tr.volumeDb) / 20.0);
            QStringList f;
            f << "asetpts=PTS-STARTPTS";
            f << QString("aresample=%1").arg(SR) << "aformat=sample_fmts=fltp:channel_layouts=stereo";
            const QString fxChain = c.fx.audio.ffmpegChain(SR);
            if (!fxChain.isEmpty())
                f << fxChain;
            f << atempoChain(c.speed);
            if (std::abs(gain - 1.0) > 1e-4)
                f << QString("volume=%1").arg(num(gain));
            const TimeUs fadeIn = std::max<TimeUs>(0, c.start + c.audio.fadeIn - a);
            const TimeUs fadeOut = std::max<TimeUs>(0, b - (c.end() - c.audio.fadeOut));
            const TimeUs dur = b - a;
            if (c.audio.fadeIn > 0 && fadeIn > 0)
                f << QString("afade=t=in:st=0:d=%1").arg(secs(std::min(fadeIn, dur)));
            if (c.audio.fadeOut > 0 && fadeOut > 0)
                f << QString("afade=t=out:st=%1:d=%2").arg(secs(std::max<TimeUs>(0, dur - fadeOut)), secs(std::min(fadeOut, dur)));
            f << "apad" << QString("atrim=end_sample=%1").arg(nSamples);
            if (delaySamples > 0)
                f << QString("adelay=delays=%1S:all=1").arg(delaySamples);
            const QString label = QString("a%1").arg(parts.size());
            graph << QString("[%1:a]%2[%3]").arg(idx).arg(f.join(','), label);
            parts << AudioPart{label, tr.duck};
        }
    }
    auto mixOf = [&](const QStringList &labels, const QString &out) {
        if (labels.size() == 1) {
            graph << QString("[%1]apad=whole_len=%2,atrim=end_sample=%2[%3]").arg(labels.first()).arg(TOTAL_SAMPLES).arg(out);
            return;
        }
        QString in;
        for (const QString &l : labels)
            in += "[" + l + "]";
        graph << QString("%1amix=inputs=%2:normalize=0:dropout_transition=0,apad=whole_len=%3,atrim=end_sample=%3[%4]")
                     .arg(in)
                     .arg(labels.size())
                     .arg(TOTAL_SAMPLES)
                     .arg(out);
    };
    QStringList voice, ducked;
    for (const AudioPart &ap : parts)
        (ap.duck ? ducked : voice) << ap.label;
    const QString finalLabel = s.normalizeLoudness ? QString("amixed") : QString("aout");
    if (parts.isEmpty()) {
        graph << QString("anullsrc=r=%1:cl=stereo,atrim=end_sample=%2[%3]").arg(SR).arg(TOTAL_SAMPLES).arg(finalLabel);
    } else if (!ducked.isEmpty() && !voice.isEmpty()) {
        // music on "duck" tracks dips automatically while the voice plays
        mixOf(voice, "avoice");
        mixOf(ducked, "amusic");
        graph << "[avoice]asplit=2[avoice1][akey]";
        graph << "[amusic][akey]sidechaincompress=threshold=0.02:ratio=8:attack=30:release=500:makeup=1[aducked]";
        graph << QString("[avoice1][aducked]amix=inputs=2:normalize=0:dropout_transition=0,atrim=end_sample=%1[%2]")
                     .arg(TOTAL_SAMPLES)
                     .arg(finalLabel);
    } else {
        QStringList all;
        for (const AudioPart &ap : parts)
            all << ap.label;
        mixOf(all, finalLabel);
    }
    if (s.normalizeLoudness)
        graph << QString("[amixed]loudnorm=I=-14:TP=-1.5:LRA=11,aresample=%1,apad=whole_len=%2,atrim=end_sample=%2[aout]")
                     .arg(SR)
                     .arg(TOTAL_SAMPLES);

    plan.filterGraph = graph.join(";\n");
    plan.filterScriptPath = scriptPath;

    // ---------------- encoder
    plan.encoderName = chooseEncoder(s.videoCodec, s.encoder, availableEncoders, plan.audioOnly ? nullptr : &plan.warnings);
    QStringList venc{"-c:v", plan.encoderName};
    const bool isSoftware = plan.encoderName.startsWith("lib");
    const int kbps = s.rateMode == "bitrate" ? s.videoBitrateKbps : estimateBitrateKbps(W, H, fps, s.crf);
    if (isSoftware && plan.encoderName != "libopenh264" && s.rateMode == "crf") {
        venc << "-crf" << QString::number(std::clamp(s.crf, 0, 51)) << "-preset" << s.speedPreset;
    } else {
        venc << "-b:v" << QString("%1k").arg(kbps) << "-maxrate" << QString("%1k").arg(int(kbps * 1.5)) << "-bufsize"
             << QString("%1k").arg(kbps * 2);
        if (isSoftware && plan.encoderName != "libopenh264")
            venc << "-preset" << s.speedPreset;
    }
    if (s.videoCodec == "hevc")
        venc << "-tag:v" << "hvc1";
    venc << "-pix_fmt" << "yuv420p" << "-r" << FPS;

    QStringList args{"-hide_banner", "-nostdin", "-y", "-progress", "pipe:1", "-nostats", "-loglevel", "warning"};
    args << inputs;
    if (ffmpegMajor >= 7)
        args << "-/filter_complex" << scriptPath;
    else
        args << "-filter_complex_script" << scriptPath;
    const QString ext = QFileInfo(s.outputPath).suffix().toLower();
    if (plan.audioOnly) {
        args << "-map" << "[aout]";
        if (ext == "mp3")
            args << "-c:a" << "libmp3lame" << "-b:a" << QString("%1k").arg(std::max(128, s.audioBitrateKbps));
        else if (ext == "wav")
            args << "-c:a" << "pcm_s16le";
        else if (ext == "flac")
            args << "-c:a" << "flac";
        else
            args << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(s.audioBitrateKbps);
        args << "-ar" << QString::number(SR);
        if (ext == "m4a")
            args << "-movflags" << "+faststart";
        plan.encoderName = "audio only";
        args << "OUTPUT_PLACEHOLDER";
        plan.args = args;
        return plan;
    }
    args << "-map" << "[vout]" << "-map" << "[aout]";
    args << venc;
    args << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(s.audioBitrateKbps) << "-ar" << QString::number(SR);
    if (ext == "mp4" || ext == "mov" || ext == "m4v")
        args << "-movflags" << "+faststart";
    args << "-frames:v" << QString::number(TOTAL);
    args << "OUTPUT_PLACEHOLDER";
    plan.args = args;
    return plan;
}

} // namespace ss
