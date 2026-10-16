#include "Tools.h"

#include <QHash>
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDir>

namespace ss {

static QStringList extraDirs()
{
    QStringList dirs;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app << app + "/bin" << app + "/../Resources/bin" << app + "/../bin";
#if defined(Q_OS_MACOS)
    dirs << "/opt/homebrew/bin" << "/usr/local/bin" << "/opt/local/bin";
#elif defined(Q_OS_WIN)
    dirs << "C:/ffmpeg/bin" << "C:/Program Files/ffmpeg/bin" << "C:/whisper.cpp/bin";
#else
    dirs << "/usr/local/bin" << "/usr/bin";
#endif
    return dirs;
}

QString Tools::find(const QStringList &names, const QString &preferred)
{
    if (!preferred.isEmpty() && QFileInfo(preferred).isExecutable())
        return preferred;
    for (const QString &dir : extraDirs())
        for (const QString &n : names) {
#if defined(Q_OS_WIN)
            const QString p = QDir(dir).filePath(n + ".exe");
#else
            const QString p = QDir(dir).filePath(n);
#endif
            if (QFileInfo(p).isFile() && QFileInfo(p).isExecutable())
                return QFileInfo(p).canonicalFilePath();
        }
    for (const QString &n : names) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty())
            return p;
    }
    return {};
}

QString Tools::ffmpeg() { return find({"ffmpeg"}, Settings::instance().ffmpegPath); }
QString Tools::ffprobe() { return find({"ffprobe"}, Settings::instance().ffprobePath); }
QString Tools::whisper()
{
    return find({"whisper-cli", "whisper-cpp", "whisper"}, Settings::instance().whisperPath);
}

Tools::RunResult Tools::run(const QString &program, const QStringList &args, int timeoutMs)
{
    RunResult r;
    if (program.isEmpty())
        return r;
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000))
        return r;
    r.started = true;
    if (!p.waitForFinished(timeoutMs)) {
        r.timedOut = true;
        p.kill();
        p.waitForFinished(2000);
    }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    return r;
}

QString Tools::ffmpegVersionString()
{
    const auto r = run(ffmpeg(), {"-hide_banner", "-version"}, 10000);
    return QString::fromUtf8(r.out).section('\n', 0, 0).trimmed();
}

int Tools::ffmpegMajorVersion()
{
    // Cached per FFmpeg path; a failed probe is not cached, so it is retried next time.
    static QMutex mutex;
    static QString cachedFor;
    static int cached = 0;
    QMutexLocker lock(&mutex);
    const QString exe = ffmpeg();
    if (exe.isEmpty())
        return 0;
    if (cachedFor == exe && cached > 0)
        return cached;
    const QString v = ffmpegVersionString();
    if (v.isEmpty())
        return 0;
    int major = 0;
    // "ffmpeg version 6.1.1-3ubuntu5", "ffmpeg version n9.0.2-27-g7e93…" (release builds)
    const auto m = QRegularExpression(R"(version\s+n?(\d+)\.)").match(v);
    if (m.hasMatch())
        major = m.captured(1).toInt();
    // Git snapshots ("ffmpeg version N-121234-gabc…", "…-git-2026…") carry no number, and some
    // distributions use odd strings. Decide by what the binary actually supports: the old
    // -filter_complex_script option was removed after 7.x, the new "-/option file" form exists since 7.0.
    if (major <= 0 || major >= 90) {
        const auto help = run(exe, {"-hide_banner", "-h", "full"}, 20000);
        if (!help.started || help.out.isEmpty())
            return 0;
        const bool oldScriptOption = help.out.contains("filter_complex_script");
        major = oldScriptOption ? 6 : 99;
    }
    cachedFor = exe;
    cached = major;
    return cached;
}

bool Tools::ffmpegUsesSlashOptions()
{
    // "-/filter_complex file" (FFmpeg 7+) instead of "-filter_complex_script file" (removed in 8)
    return ffmpegFilterScriptOption() == "-/filter_complex";
}

QString Tools::ffmpegFilterScriptOption()
{
    static QMutex mutex;
    static QString cachedFor, cached;
    QMutexLocker lock(&mutex);
    const QString exe = ffmpeg();
    if (exe.isEmpty())
        return "-/filter_complex";
    if (cachedFor == exe && !cached.isEmpty())
        return cached;
    // Ask the real binary: run a one-frame job with each spelling, the predicted one first.
    QTemporaryFile script(QDir::tempPath() + "/shyftr-probe-XXXXXX.txt");
    QString found;
    if (script.open()) {
        script.write("[0:v]null[out]");
        script.flush();
        QStringList order{"-/filter_complex", "-filter_complex_script"};
        if (ffmpegMajorVersion() > 0 && ffmpegMajorVersion() < 7)
            std::swap(order[0], order[1]);
        for (const QString &opt : order) {
            const auto r = run(exe, {"-hide_banner", "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
                                     "color=black:s=16x16:d=0.04", opt, script.fileName(), "-map", "[out]",
                                     "-frames:v", "1", "-f", "null", "-"},
                               20000);
            if (r.started && r.exitCode == 0) {
                found = opt;
                break;
            }
        }
    }
    if (found.isEmpty()) // could not tell (probe failed for another reason): go by the version
        return ffmpegMajorVersion() >= 7 || ffmpegMajorVersion() == 0 ? "-/filter_complex" : "-filter_complex_script";
    cachedFor = exe;
    cached = found;
    return cached;
}

QStringList Tools::ffmpegEncoders()
{
    static QMutex mutex;
    static QStringList cached;
    static bool done = false;
    QMutexLocker lock(&mutex);
    if (done)
        return cached;
    const auto r = run(ffmpeg(), {"-hide_banner", "-encoders"}, 15000);
    const QStringList lines = QString::fromUtf8(r.out).split('\n');
    QRegularExpression re(R"(^\s*[VAS][\.FSXBD]{5}\s+(\S+))");
    for (const QString &l : lines) {
        const auto m = re.match(l);
        if (m.hasMatch())
            cached << m.captured(1);
    }
    done = r.started;
    return cached;
}

bool Tools::hasEncoder(const QString &name)
{
    return ffmpegEncoders().contains(name);
}

bool Tools::encoderWorks(const QString &name)
{
    // An encoder can be compiled in but unusable (no GPU / driver). Try a tiny encode once.
    static QMutex mutex;
    static QHash<QString, bool> cache;
    QMutexLocker lock(&mutex);
    auto it = cache.find(name);
    if (it != cache.end())
        return it.value();
    const auto r = run(ffmpeg(), {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=black:s=256x256:r=30:d=0.2",
                                  "-c:v", name, "-pix_fmt", "yuv420p", "-f", "null", "-"},
                       20000);
    const bool ok = r.started && !r.timedOut && r.exitCode == 0;
    cache.insert(name, ok);
    return ok;
}

QString Tools::whisperVersionString()
{
    const QString w = whisper();
    if (w.isEmpty())
        return {};
    const auto r = run(w, {"--help"}, 10000);
    return r.started ? QFileInfo(w).fileName() + " (available)" : QString();
}

} // namespace ss
