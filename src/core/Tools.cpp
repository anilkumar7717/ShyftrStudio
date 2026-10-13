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
    static int cached = -2;
    if (cached != -2)
        return cached;
    const QString v = ffmpegVersionString();
    QRegularExpression re(R"(version\s+n?(\d+))");
    const auto m = re.match(v);
    cached = m.hasMatch() ? m.captured(1).toInt() : 0;
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
