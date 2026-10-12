#pragma once

#include "core/Model.h"
#include "core/Settings.h"
#include "core/TimelineOps.h"
#include "core/Tools.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

namespace ss::test {

inline MediaAsset fakeVideo(const QString &id, TimeUs dur, bool audio = true)
{
    MediaAsset m;
    m.id = id;
    m.name = id + ".mp4";
    m.path = "/nonexistent/" + id + ".mp4";
    m.kind = MediaKind::Video;
    m.duration = dur;
    m.width = 1920;
    m.height = 1080;
    m.hasVideo = true;
    m.hasAudio = audio;
    m.audioChannels = 2;
    m.sampleRate = 48000;
    return m;
}

inline Word W(const QString &t, int startMs, int endMs, float c = 0.9f)
{
    Word w;
    w.text = t;
    w.start = TimeUs(startMs) * kUsPerMs;
    w.end = TimeUs(endMs) * kUsPerMs;
    w.confidence = c;
    return w;
}

// Generates a synthetic test clip with FFmpeg (lavfi) - returns empty if FFmpeg is missing.
inline QString makeTestVideo(const QString &dir, const QString &name, double seconds, int w = 640, int h = 360,
                             const QString &rate = "30")
{
    const QString ffmpeg = Tools::ffmpeg();
    if (ffmpeg.isEmpty())
        return {};
    const QString out = dir + "/" + name;
    const auto r = Tools::run(ffmpeg,
                              {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                               QString("testsrc2=s=%1x%2:r=%3:d=%4").arg(w).arg(h).arg(rate).arg(seconds), "-f", "lavfi",
                               "-i", QString("sine=f=440:d=%1:sample_rate=48000").arg(seconds), "-c:v", "libx264", "-preset",
                               "ultrafast", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", out},
                              120000);
    return r.exitCode == 0 ? out : QString();
}

} // namespace ss::test
