#include "ExportJob.h"
#include "../captions/SubtitleIO.h"
#include "../core/Tools.h"
#include "Compositor.h"
#include "FrameReader.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>
#include <algorithm>

namespace ss {

ExportJob::ExportJob(ExportRequest req, QObject *parent) : QThread(parent), m_req(std::move(req)) {}

void ExportJob::run()
{
    QElapsedTimer clock;
    clock.start();
    const ExportSettings &s = m_req.settings;
    const QString logDir = m_req.projectDir.isEmpty() ? QDir::tempPath() : m_req.projectDir + "/exports/logs";
    QDir().mkpath(logDir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    m_logPath = logDir + "/export-" + stamp + ".log";
    const QString scriptPath = logDir + "/export-" + stamp + ".filtergraph.txt";

    QFile log(m_logPath);
    log.open(QIODevice::WriteOnly | QIODevice::Text);
    auto L = [&log](const QString &line) {
        log.write(line.toUtf8());
        log.write("\n");
        log.flush();
    };
    auto failWith = [&](const QString &msg) {
        L("FAILED: " + msg);
        log.close();
        emit failedWithReport(msg, m_logPath);
    };

    const QString ffmpeg = Tools::ffmpeg();
    if (ffmpeg.isEmpty())
        return failWith("FFmpeg was not found. Install it or set its location in Preferences → Tools.");
    if (s.outputPath.isEmpty())
        return failWith("No output file was chosen.");
    QDir().mkpath(QFileInfo(s.outputPath).absolutePath());

    const QString outExt = QFileInfo(s.outputPath).suffix().toLower();
    const bool gif = outExt == "gif";
    const bool audioOnly = ExportGraph::isAudioOnlyPath(s.outputPath);
    const bool compositor = !audioOnly && (m_req.forceCompositor || Compositor::projectNeedsCompositor(m_req.project));
    const bool needsOverlay = !audioOnly && !compositor && !m_req.overlay.isEmpty();
    ExportSettings renderSettings = s;
    if (gif) // render an intermediate video, then convert it to an optimised palette GIF
        renderSettings.outputPath = QFileInfo(s.outputPath).absolutePath() + "/" + QFileInfo(s.outputPath).completeBaseName() + ".gifsrc.mp4";
    m_plan = ExportGraph::build(m_req.project, renderSettings, needsOverlay, Tools::ffmpegMajorVersion(), Tools::ffmpegEncoders(),
                                scriptPath, compositor);
    if (!m_plan.error.isEmpty())
        return failWith(m_plan.error);
    if (!m_plan.audioOnly && !m_plan.encoderName.startsWith("lib") && !Tools::encoderWorks(m_plan.encoderName)) {
        // Listed by FFmpeg but unusable on this computer (no GPU/driver): use the software encoder.
        const QString failed = m_plan.encoderName;
        renderSettings.encoder = "software";
        m_plan = ExportGraph::build(m_req.project, renderSettings, needsOverlay, Tools::ffmpegMajorVersion(), Tools::ffmpegEncoders(),
                                    scriptPath, compositor);
        m_plan.warnings << QString("The hardware encoder %1 is not usable on this computer; the software encoder was used.").arg(failed);
        if (!m_plan.error.isEmpty())
            return failWith(m_plan.error);
    }

    // disk space estimate: bitrate * duration (+ 10%)
    const int kbps = s.rateMode == "bitrate" ? s.videoBitrateKbps
                                             : ExportGraph::estimateBitrateKbps(s.width, s.height, s.fps, s.crf);
    const qint64 estBytes = qint64(((audioOnly ? 0 : kbps) + s.audioBitrateKbps) * 1000.0 / 8.0 * m_plan.duration / kUsPerSec * 1.1);
    QStorageInfo si(QFileInfo(s.outputPath).absolutePath());
    if (si.isValid() && si.bytesAvailable() >= 0 && si.bytesAvailable() < estBytes)
        return failWith(QString("Not enough disk space at the destination (about %1 MB needed).").arg(estBytes >> 20));

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text))
        return failWith("Could not write the render graph to " + scriptPath);
    script.write(m_plan.filterGraph.toUtf8());
    script.close();

    const QFileInfo outInfo(s.outputPath);
    const QString partial = outInfo.absolutePath() + "/" + outInfo.completeBaseName() + ".partial." + (gif ? QString("mp4") : outInfo.suffix());
    QStringList args = m_plan.args;
    args.replace(args.indexOf("OUTPUT_PLACEHOLDER"), partial);
    // Use the filter-script option this FFmpeg really accepts (version strings of snapshot
    // builds can't be trusted; -filter_complex_script no longer exists in FFmpeg 8+).
    for (int i = 0; i < args.size(); ++i)
        if (args[i] == "-/filter_complex" || args[i] == "-filter_complex_script")
            args[i] = Tools::ffmpegFilterScriptOption();

    L("Shyftr Studio export " + QDateTime::currentDateTime().toString(Qt::ISODate));
    L("ffmpeg: " + ffmpeg + " (" + Tools::ffmpegVersionString() + ")");
    L("encoder: " + m_plan.encoderName);
    L(QString("frames: %1, duration: %2 s, overlay: %3, compositor: %4, audio only: %5")
          .arg(m_plan.frameCount)
          .arg(m_plan.duration / 1e6)
          .arg(needsOverlay)
          .arg(compositor)
          .arg(audioOnly));
    for (const QString &w : m_plan.warnings)
        L("warning: " + w);
    L("args: " + args.join(' '));
    L("filtergraph: " + scriptPath);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(10000))
        return failWith("FFmpeg could not be started.");

    QByteArray errAll;
    qint64 encodedFrames = 0;
    auto pump = [&]() {
        const QByteArray out = proc.readAllStandardOutput();
        for (const QByteArray &line : out.split('\n')) {
            if (line.startsWith("frame="))
                encodedFrames = line.mid(6).trimmed().toLongLong();
        }
        const QByteArray err = proc.readAllStandardError();
        if (!err.isEmpty()) {
            errAll += err;
            log.write(err);
            log.flush();
        }
    };
    double lastEmit = -1;
    auto report = [&](qint64 producedFrames, const QString &status) {
        const double elapsed = clock.elapsed() / 1000.0;
        const qint64 done = std::max(encodedFrames, std::min(producedFrames, encodedFrames + 30));
        const double frac = m_plan.frameCount ? std::clamp(double(done) / m_plan.frameCount, 0.0, 1.0) : 0.0;
        if (frac - lastEmit < 0.002 && frac < 1.0)
            return;
        lastEmit = frac;
        const double fps = elapsed > 0 ? done / elapsed : 0;
        const double eta = fps > 0 ? (m_plan.frameCount - done) / fps : -1;
        emit progressChanged(frac, fps, eta, status);
    };
    auto cancelled = [&]() {
        proc.kill();
        proc.waitForFinished(5000);
        QFile::remove(partial);
        L("Cancelled by user.");
        log.close();
        emit cancelledByUser();
    };

    if (compositor) {
        // Every frame is drawn by the same compositor as the preview and piped to FFmpeg.
        const int W = s.width & ~1, H = s.height & ~1;
        const qsizetype frameBytes = qsizetype(W) * H * 4;
        FrameReader reader(m_req.project, QSize(W, H));
        Compositor::Options copt;
        copt.missingPlaceholder = false;
        for (qint64 f = 0; f < m_plan.frameCount; ++f) {
            if (m_cancel)
                return cancelled();
            if (proc.state() != QProcess::Running)
                break;
            const TimeUs t = s.rangeStart + framesToUs(f, s.fps);
            QImage canvas(W, H, QImage::Format_ARGB32_Premultiplied);
            Compositor::render(canvas, m_req.project, m_req.overlay, t, reader, copt);
            QSet<QString> keep;
            for (const auto &n : Compositor::needsAt(m_req.project, t + framesToUs(1, s.fps)))
                keep.insert(n.clipId);
            for (const auto &n : Compositor::needsAt(m_req.project, t))
                keep.insert(n.clipId);
            reader.retain(keep);
            const QImage img = canvas.convertToFormat(QImage::Format_RGBA8888);
            if (img.bytesPerLine() == W * 4)
                proc.write(reinterpret_cast<const char *>(img.constBits()), frameBytes);
            else
                for (int y = 0; y < H; ++y)
                    proc.write(reinterpret_cast<const char *>(img.constScanLine(y)), W * 4);
            while (proc.bytesToWrite() > 64LL * 1024 * 1024 && proc.state() == QProcess::Running) {
                proc.waitForBytesWritten(100);
                pump();
                if (m_cancel)
                    return cancelled();
            }
            proc.waitForBytesWritten(0);
            pump();
            report(f, "Rendering");
        }
        if (!reader.lastError().isEmpty())
            L("decoder: " + reader.lastError());
        while (proc.bytesToWrite() > 0 && proc.state() == QProcess::Running) {
            proc.waitForBytesWritten(200);
            pump();
            if (m_cancel)
                return cancelled();
        }
        proc.closeWriteChannel();
    } else if (needsOverlay) {
        const int W = s.width & ~1, H = s.height & ~1;
        const QSize size(W, H);
        const qsizetype frameBytes = qsizetype(W) * H * 4;
        const QByteArray empty(frameBytes, '\0');
        const Rational fps = s.fps;
        for (qint64 f = 0; f < m_plan.frameCount; ++f) {
            if (m_cancel)
                return cancelled();
            if (proc.state() != QProcess::Running)
                break;
            const TimeUs t = s.rangeStart + framesToUs(f, fps);
            if (m_req.overlay.isEmptyAt(t)) {
                proc.write(empty);
            } else {
                QImage img = m_req.overlay.renderImage(size, t).convertToFormat(QImage::Format_RGBA8888);
                if (img.bytesPerLine() == W * 4)
                    proc.write(reinterpret_cast<const char *>(img.constBits()), frameBytes);
                else
                    for (int y = 0; y < H; ++y)
                        proc.write(reinterpret_cast<const char *>(img.constScanLine(y)), W * 4);
            }
            while (proc.bytesToWrite() > 64LL * 1024 * 1024 && proc.state() == QProcess::Running) {
                proc.waitForBytesWritten(100);
                pump();
                if (m_cancel)
                    return cancelled();
            }
            proc.waitForBytesWritten(0);
            pump();
            report(f, "Rendering");
        }
        while (proc.bytesToWrite() > 0 && proc.state() == QProcess::Running) {
            proc.waitForBytesWritten(200);
            pump();
            if (m_cancel)
                return cancelled();
        }
        proc.closeWriteChannel();
    } else {
        proc.closeWriteChannel();
    }

    while (!proc.waitForFinished(200)) {
        pump();
        report(encodedFrames, "Encoding");
        if (m_cancel)
            return cancelled();
        if (proc.state() == QProcess::NotRunning)
            break;
    }
    pump();

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        QFile::remove(partial);
        QString tail = QString::fromUtf8(errAll).trimmed();
        tail = tail.right(800);
        return failWith("FFmpeg reported an error while rendering." + (tail.isEmpty() ? QString() : "\n\n" + tail));
    }
    if (QFileInfo(partial).size() == 0) {
        QFile::remove(partial);
        return failWith("The rendered file is empty.");
    }
    if (gif) {
        emit progressChanged(0.99, 0, 0, "Making GIF");
        const QString gifPartial = outInfo.absolutePath() + "/" + outInfo.completeBaseName() + ".partial.gif";
        const int gifW = std::min(720, s.width & ~1);
        const auto r = Tools::run(ffmpeg,
                                  {"-hide_banner", "-loglevel", "error", "-y", "-i", partial, "-vf",
                                   QString("fps=15,scale=%1:-2:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];"
                                           "[b][p]paletteuse=dither=bayer:bayer_scale=4")
                                       .arg(gifW),
                                   "-loop", "0", gifPartial},
                                  30 * 60 * 1000);
        QFile::remove(partial);
        if (r.exitCode != 0) {
            QFile::remove(gifPartial);
            return failWith("FFmpeg could not convert the video to GIF.\n\n" + QString::fromUtf8(r.err).right(600));
        }
        QFile::remove(s.outputPath);
        if (!QFile::rename(gifPartial, s.outputPath))
            return failWith("The GIF was rendered but could not be moved to " + s.outputPath);
    } else {
        QFile::remove(s.outputPath);
        if (!QFile::rename(partial, s.outputPath))
            return failWith("The video was rendered but could not be moved to " + s.outputPath);
    }

    QStringList warnings = m_plan.warnings;
    const QString base = outInfo.absolutePath() + "/" + outInfo.completeBaseName();
    QString err;
    if (!m_req.srtText.isEmpty() && !SubtitleIO::writeTextFile(base + ".srt", m_req.srtText, &err))
        warnings << "SRT sidecar could not be written: " + err;
    if (!m_req.vttText.isEmpty() && !SubtitleIO::writeTextFile(base + ".vtt", m_req.vttText, &err))
        warnings << "VTT sidecar could not be written: " + err;

    const double secs = clock.elapsed() / 1000.0;
    L(QString("Done in %1 s").arg(secs, 0, 'f', 1));
    log.close();
    emit progressChanged(1.0, 0, 0, "Done");
    emit succeeded(s.outputPath, secs, warnings);
}

} // namespace ss
