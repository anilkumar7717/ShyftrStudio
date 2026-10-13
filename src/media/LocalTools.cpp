#include "LocalTools.h"
#include "../core/Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace ss {
namespace LocalTools {

namespace {

QString secs(TimeUs t)
{
    return QString::number(double(t) / kUsPerSec, 'f', 6);
}

Result fail(const QString &msg)
{
    Result r;
    r.error = msg;
    return r;
}

// Runs FFmpeg with progress reporting. expectedUs is the duration being produced.
Result runFfmpeg(const QStringList &args, TimeUs expectedUs, const Progress &progress, const QString &workDir = {},
                 QByteArray *stderrOut = nullptr, QByteArray *stdoutOut = nullptr, bool progressOnStdout = true)
{
    const QString ffmpeg = Tools::ffmpeg();
    if (ffmpeg.isEmpty())
        return fail("FFmpeg was not found. Install it or set its location in Preferences → Tools.");
    QProcess proc;
    if (!workDir.isEmpty())
        proc.setWorkingDirectory(workDir);
    QStringList all{"-hide_banner", "-nostdin", "-y"};
    if (progressOnStdout)
        all << "-progress" << "pipe:1" << "-nostats";
    all << args;
    proc.start(ffmpeg, all);
    if (!proc.waitForStarted(10000))
        return fail("FFmpeg could not be started.");
    QByteArray err, out, lineBuf;
    while (!proc.waitForFinished(200)) {
        err += proc.readAllStandardError();
        const QByteArray chunk = proc.readAllStandardOutput();
        if (!progressOnStdout) {
            out += chunk;
        } else {
            lineBuf += chunk;
            int nl;
            while ((nl = lineBuf.indexOf('\n')) >= 0) {
                const QByteArray line = lineBuf.left(nl).trimmed();
                lineBuf.remove(0, nl + 1);
                if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
                    const qint64 us = line.mid(line.indexOf('=') + 1).toLongLong();
                    if (progress && expectedUs > 0 && !progress(std::clamp(double(us) / expectedUs, 0.0, 1.0))) {
                        proc.kill();
                        proc.waitForFinished(3000);
                        return fail("Cancelled.");
                    }
                }
            }
        }
        if (progress && expectedUs <= 0 && !progress(-1)) {
            proc.kill();
            proc.waitForFinished(3000);
            return fail("Cancelled.");
        }
        if (proc.state() == QProcess::NotRunning)
            break;
    }
    err += proc.readAllStandardError();
    if (!progressOnStdout)
        out += proc.readAllStandardOutput();
    if (stderrOut)
        *stderrOut = err;
    if (stdoutOut)
        *stdoutOut = out;
    Result r;
    r.ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!r.ok)
        r.error = "FFmpeg failed:\n" + QString::fromUtf8(err).trimmed().right(700);
    return r;
}

QStringList videoEncodeArgs()
{
    if (Tools::hasEncoder("libx264"))
        return {"-c:v", "libx264", "-preset", "veryfast", "-crf", "16", "-pix_fmt", "yuv420p"};
    if (Tools::hasEncoder("libopenh264"))
        return {"-c:v", "libopenh264", "-b:v", "25M", "-pix_fmt", "yuv420p"};
    return {"-c:v", "mpeg4", "-q:v", "2"};
}

bool moveInto(const QString &tmp, const QString &dst, QString *err)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (!QFile::rename(tmp, dst) && !QFile::copy(tmp, dst)) {
        if (err)
            *err = "Could not write " + dst;
        return false;
    }
    return true;
}

} // namespace

bool ffmpegHasFilter(const QString &name)
{
    static QStringList cached;
    static bool done = false;
    if (!done) {
        const auto r = Tools::run(Tools::ffmpeg(), {"-hide_banner", "-filters"}, 15000);
        for (const QString &l : QString::fromUtf8(r.out).split('\n')) {
            const QStringList parts = l.simplified().split(' ');
            if (parts.size() >= 2)
                cached << parts[1];
        }
        done = r.started;
    }
    return cached.contains(name);
}

// ------------------------------------------------------------------ reverse

Result reverse(const QString &src, TimeUs in, TimeUs out, bool hasAudio, const QString &dst, const Progress &progress)
{
    if (out <= in)
        return fail("Nothing to reverse.");
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail("No temporary folder available.");
    const TimeUs chunk = 4 * kUsPerSec;
    QStringList parts;
    const TimeUs total = out - in;
    int i = 0;
    for (TimeUs s = in; s < out; s += chunk, ++i) {
        const TimeUs d = std::min(chunk, out - s);
        const QString part = tmp.filePath(QString("part%1.mp4").arg(i, 5, 10, QChar('0')));
        QStringList args{"-ss", secs(s), "-t", secs(d), "-i", src, "-vf", "reverse"};
        if (hasAudio)
            args << "-af" << "areverse" << "-c:a" << "aac" << "-b:a" << "192k" << "-ar" << "48000";
        else
            args << "-an";
        args << videoEncodeArgs() << part;
        const TimeUs done = s - in;
        const Result r = runFfmpeg(args, d, [&](double f) { return !progress || progress((done + f * d) / double(total) * 0.95); });
        if (!r.ok)
            return r;
        parts.prepend(part); // last chunk first
    }
    const QString list = tmp.filePath("list.txt");
    {
        QFile f(list);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return fail("Could not write the chunk list.");
        QTextStream ts(&f);
        for (const QString &p : parts)
            ts << "file '" << QFileInfo(p).fileName() << "'\n";
    }
    const QString joined = tmp.filePath("joined.mp4");
    const Result r = runFfmpeg({"-f", "concat", "-safe", "0", "-i", "list.txt", "-c", "copy", "-movflags", "+faststart", joined}, total,
                               {}, tmp.path());
    if (!r.ok)
        return r;
    Result res;
    if (!moveInto(joined, dst, &res.error))
        return res;
    if (progress)
        progress(1.0);
    res.ok = true;
    res.path = dst;
    return res;
}

// ------------------------------------------------------------------ stabilise

Result stabilize(const QString &src, TimeUs in, TimeUs out, bool hasAudio, const QString &dst, const Progress &progress)
{
    if (out <= in)
        return fail("Nothing to stabilise.");
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail("No temporary folder available.");
    const TimeUs d = out - in;
    const QString outTmp = tmp.filePath("stable.mp4");
    QStringList audio = hasAudio ? QStringList{"-c:a", "aac", "-b:a", "192k", "-ar", "48000"} : QStringList{"-an"};
    if (ffmpegHasFilter("vidstabdetect") && ffmpegHasFilter("vidstabtransform")) {
        // pass 1: analyse motion (written next to the working directory to avoid path escaping)
        Result r = runFfmpeg({"-ss", secs(in), "-t", secs(d), "-i", src, "-an", "-vf",
                              "vidstabdetect=shakiness=6:accuracy=15:result=transforms.trf", "-f", "null", "-"},
                             d, [&](double f) { return !progress || progress(f * 0.45); }, tmp.path());
        if (!r.ok)
            return r;
        QStringList args{"-ss", secs(in), "-t", secs(d), "-i", src, "-vf",
                         "vidstabtransform=input=transforms.trf:smoothing=20:zoom=3:optzoom=0,unsharp=5:5:0.6:3:3:0.4"};
        args << audio << videoEncodeArgs() << outTmp;
        r = runFfmpeg(args, d, [&](double f) { return !progress || progress(0.45 + f * 0.55); }, tmp.path());
        if (!r.ok)
            return r;
    } else {
        QStringList args{"-ss", secs(in), "-t", secs(d), "-i", src, "-vf", "deshake=rx=32:ry=32,scale=iw*1.04:-2,crop=iw/1.04:ih/1.04"};
        args << audio << videoEncodeArgs() << outTmp;
        const Result r = runFfmpeg(args, d, progress, tmp.path());
        if (!r.ok)
            return r;
    }
    Result res;
    if (!moveInto(outTmp, dst, &res.error))
        return res;
    res.ok = true;
    res.path = dst;
    return res;
}

// ------------------------------------------------------------------ extract

Result extractAudio(const QString &src, TimeUs in, TimeUs out, const QString &dst)
{
    const QString ext = QFileInfo(dst).suffix().toLower();
    QStringList codec;
    if (ext == "wav")
        codec = {"-c:a", "pcm_s16le"};
    else if (ext == "mp3")
        codec = {"-c:a", "libmp3lame", "-b:a", "192k"};
    else
        codec = {"-c:a", "aac", "-b:a", "192k"};
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QStringList args{"-ss", secs(in), "-t", secs(std::max<TimeUs>(1000, out - in)), "-i", src, "-vn"};
    args << codec << "-ar" << "48000" << dst;
    Result r = runFfmpeg(args, out - in, {});
    if (r.ok)
        r.path = dst;
    return r;
}

Result extractFrame(const QString &src, TimeUs at, const QString &dstPng)
{
    QDir().mkpath(QFileInfo(dstPng).absolutePath());
    Result r = runFfmpeg({"-ss", secs(at), "-i", src, "-frames:v", "1", "-update", "1", dstPng}, 0, {});
    if (r.ok && !QFileInfo::exists(dstPng)) {
        r.ok = false;
        r.error = "No frame could be read at that time.";
    }
    if (r.ok)
        r.path = dstPng;
    return r;
}

// ------------------------------------------------------------------ analysis

QVector<QPair<TimeUs, TimeUs>> silences(const QString &src, TimeUs in, TimeUs out, double thresholdDb, TimeUs minDuration,
                                        TimeUs padding, QString *error)
{
    QVector<QPair<TimeUs, TimeUs>> result;
    QByteArray err;
    const Result r = runFfmpeg({"-ss", secs(in), "-t", secs(out - in), "-i", src, "-vn", "-af",
                                QString("silencedetect=noise=%1dB:d=%2").arg(thresholdDb, 0, 'f', 1).arg(double(minDuration) / kUsPerSec, 0, 'f', 3),
                                "-f", "null", "-"},
                               out - in, {}, {}, &err);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return result;
    }
    QRegularExpression reStart("silence_start: (-?[0-9.]+)"), reEnd("silence_end: (-?[0-9.]+)");
    double start = -1;
    for (const QString &line : QString::fromUtf8(err).split('\n')) {
        auto ms = reStart.match(line);
        if (ms.hasMatch())
            start = std::max(0.0, ms.captured(1).toDouble());
        auto me = reEnd.match(line);
        if (me.hasMatch() && start >= 0) {
            const double end = me.captured(1).toDouble();
            TimeUs a = in + TimeUs(start * kUsPerSec) + padding;
            TimeUs b = in + TimeUs(end * kUsPerSec) - padding;
            if (start <= 0.001)
                a = in; // leading silence: cut from the very start
            if (b - a >= 150 * kUsPerMs)
                result << qMakePair(a, std::min(b, out));
            start = -1;
        }
    }
    if (start >= 0) { // trailing silence until the end
        const TimeUs a = in + TimeUs(start * kUsPerSec) + padding;
        if (out - a >= 150 * kUsPerMs)
            result << qMakePair(a, out);
    }
    return result;
}

QVector<TimeUs> scenes(const QString &src, TimeUs in, TimeUs out, double threshold, QString *error)
{
    QVector<TimeUs> result;
    QByteArray err;
    const Result r = runFfmpeg({"-ss", secs(in), "-t", secs(out - in), "-i", src, "-an", "-vf",
                                QString("scale=320:-2,select='gt(scene,%1)',showinfo").arg(threshold, 0, 'f', 3), "-f", "null", "-"},
                               out - in, {}, {}, &err);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return result;
    }
    QRegularExpression re("pts_time:([0-9.]+)");
    for (const QString &line : QString::fromUtf8(err).split('\n')) {
        if (!line.contains("showinfo"))
            continue;
        const auto m = re.match(line);
        if (m.hasMatch()) {
            const TimeUs t = in + TimeUs(m.captured(1).toDouble() * kUsPerSec);
            if (t > in + 300 * kUsPerMs && t < out - 300 * kUsPerMs && (result.isEmpty() || t - result.last() > 500 * kUsPerMs))
                result << t;
        }
    }
    return result;
}

QVector<double> detectOnsets(const QVector<float> &x, int sr, double sensitivity)
{
    QVector<double> times;
    const int hop = 512;
    const int n = int(x.size()) / hop;
    if (n < 8)
        return times;
    QVector<double> energy(n), flux(n, 0.0);
    double peakEnergy = 0;
    for (int i = 0; i < n; ++i) {
        double e = 0;
        for (int k = 0; k < hop; ++k) {
            const double v = x[i * hop + k];
            e += v * v;
        }
        energy[i] = e / hop;
        peakEnergy = std::max(peakEnergy, energy[i]);
    }
    for (int i = 1; i < n; ++i)
        flux[i] = std::max(0.0, std::log(1e-9 + energy[i]) - std::log(1e-9 + energy[i - 1]));
    const int W = std::max(4, int(0.5 * sr / hop));
    const double k = 1.3 + (1.0 - std::clamp(sensitivity, 0.0, 1.0)) * 1.7;
    const double minGap = 0.2;
    double last = -1;
    for (int i = 1; i < n - 1; ++i) {
        double sum = 0, cnt = 0;
        for (int j = std::max(1, i - W); j <= std::min(n - 1, i + W); ++j) {
            sum += flux[j];
            ++cnt;
        }
        const double thr = (sum / cnt) * k + 0.05;
        bool isMax = true;
        for (int j = std::max(1, i - 3); j <= std::min(n - 1, i + 3); ++j)
            if (flux[j] > flux[i])
                isMax = false;
        const double t = double(i) * hop / sr;
        if (isMax && flux[i] > thr && energy[i] > peakEnergy * 0.002 && (last < 0 || t - last >= minGap)) {
            times << t;
            last = t;
        }
    }
    return times;
}

QVector<TimeUs> beats(const QString &src, TimeUs in, TimeUs out, double sensitivity, QString *error)
{
    QVector<TimeUs> result;
    QByteArray pcm;
    const Result r = runFfmpeg({"-ss", secs(in), "-t", secs(out - in), "-i", src, "-vn", "-ac", "1", "-ar", "11025", "-f", "s16le", "-"},
                               0, {}, {}, nullptr, &pcm, false);
    if (!r.ok) {
        if (error)
            *error = r.error;
        return result;
    }
    QVector<float> mono(pcm.size() / 2);
    const qint16 *s = reinterpret_cast<const qint16 *>(pcm.constData());
    for (int i = 0; i < mono.size(); ++i)
        mono[i] = s[i] / 32768.0f;
    for (double t : detectOnsets(mono, 11025, sensitivity))
        result << in + TimeUs(t * kUsPerSec);
    return result;
}

// ------------------------------------------------------------------ text to speech

QString speechEngineName()
{
#if defined(Q_OS_MACOS)
    return "macOS voices";
#elif defined(Q_OS_WIN)
    return "Windows voices";
#else
    return QStandardPaths::findExecutable("espeak-ng").isEmpty() ? (QStandardPaths::findExecutable("espeak").isEmpty() ? QString() : "eSpeak")
                                                                  : "eSpeak NG";
#endif
}

QStringList voices()
{
    QStringList out;
#if defined(Q_OS_MACOS)
    const auto r = Tools::run("/usr/bin/say", {"-v", "?"}, 10000);
    QRegularExpression re(R"(^(.+?)\s+([a-z]{2,3}[_-][A-Za-z0-9]+)\s+#)");
    for (const QString &line : QString::fromUtf8(r.out).split('\n')) {
        const auto m = re.match(line);
        if (m.hasMatch())
            out << m.captured(1).trimmed() + " — " + m.captured(2);
    }
#elif defined(Q_OS_WIN)
    const auto r = Tools::run("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                                             "Add-Type -AssemblyName System.Speech; (New-Object System.Speech.Synthesis.SpeechSynthesizer)."
                                             "GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name + ' — ' + $_.VoiceInfo.Culture.Name }"},
                              20000);
    for (const QString &line : QString::fromUtf8(r.out).split('\n'))
        if (!line.trimmed().isEmpty())
            out << line.trimmed();
#else
    QString exe = QStandardPaths::findExecutable("espeak-ng");
    if (exe.isEmpty())
        exe = QStandardPaths::findExecutable("espeak");
    if (!exe.isEmpty()) {
        const auto r = Tools::run(exe, {"--voices"}, 10000);
        for (const QString &line : QString::fromUtf8(r.out).split('\n').mid(1)) {
            const QStringList p = line.simplified().split(' ');
            if (p.size() >= 4)
                out << p[3] + " — " + p[1];
        }
    }
#endif
    return out;
}

Result textToSpeech(const QString &text, const QString &voiceLabel, double rate, const QString &dstWav)
{
    if (text.trimmed().isEmpty())
        return fail("There is no text to speak.");
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail("No temporary folder available.");
    const QString txt = tmp.filePath("text.txt");
    {
        QFile f(txt);
        if (!f.open(QIODevice::WriteOnly))
            return fail("Could not write the text.");
        f.write(text.toUtf8());
    }
    const QString voice = voiceLabel.section(" — ", 0, 0).trimmed();
    QString raw;
    Tools::RunResult r;
#if defined(Q_OS_MACOS)
    raw = tmp.filePath("speech.aiff");
    QStringList args{"-o", raw, "-f", txt, "-r", QString::number(int(std::lround(175 * std::clamp(rate, 0.5, 2.0))))};
    if (!voice.isEmpty())
        args << "-v" << voice;
    r = Tools::run("/usr/bin/say", args, 300000);
#elif defined(Q_OS_WIN)
    raw = tmp.filePath("speech.wav");
    auto q = [](QString s) { return "'" + s.replace("'", "''") + "'"; };
    const int sapiRate = std::clamp(int(std::lround((rate - 1.0) * 10)), -10, 10);
    const QString script = QString("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
                                   "%1 $s.Rate = %2; $s.SetOutputToWaveFile(%3); "
                                   "$s.Speak([IO.File]::ReadAllText(%4, [Text.Encoding]::UTF8)); $s.Dispose()")
                               .arg(voice.isEmpty() ? QString() : QString("$s.SelectVoice(%1);").arg(q(voice)))
                               .arg(sapiRate)
                               .arg(q(QDir::toNativeSeparators(raw)), q(QDir::toNativeSeparators(txt)));
    r = Tools::run("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script}, 300000);
#else
    raw = tmp.filePath("speech.wav");
    QString exe = QStandardPaths::findExecutable("espeak-ng");
    if (exe.isEmpty())
        exe = QStandardPaths::findExecutable("espeak");
    if (exe.isEmpty())
        return fail("No speech engine was found. Install espeak-ng (sudo apt install espeak-ng).");
    QStringList args{"-w", raw, "-f", txt, "-s", QString::number(int(std::lround(175 * std::clamp(rate, 0.5, 2.0))))};
    if (!voice.isEmpty())
        args << "-v" << voice;
    r = Tools::run(exe, args, 300000);
#endif
    if (!r.started || r.exitCode != 0 || !QFileInfo::exists(raw) || QFileInfo(raw).size() < 100)
        return fail("The computer's speech engine could not create audio.\n" + QString::fromUtf8(r.err).trimmed().right(400));
    const QString conv = tmp.filePath("out.wav");
    const Result c = runFfmpeg({"-i", raw, "-ar", "48000", "-ac", "2", "-c:a", "pcm_s16le", conv}, 0, {});
    if (!c.ok)
        return c;
    Result res;
    if (!moveInto(conv, dstWav, &res.error))
        return res;
    res.ok = true;
    res.path = dstWav;
    return res;
}

} // namespace LocalTools
} // namespace ss
