#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ss {

// Locates the bundled/local command-line engines. Nothing is downloaded.
// Search order: user preference -> next to the executable (and Contents/Resources/bin on macOS)
// -> PATH -> common package-manager folders (/opt/homebrew/bin, /usr/local/bin, C:\ffmpeg\bin).
class Tools {
public:
    static QString ffmpeg();
    static QString ffprobe();
    static QString whisper();
    static QString find(const QStringList &names, const QString &preferred);

    struct RunResult {
        int exitCode = -1;
        bool started = false;
        bool timedOut = false;
        QByteArray out;
        QByteArray err;
    };
    // Synchronous helper for short-lived probes (ffprobe, -version, -encoders).
    static RunResult run(const QString &program, const QStringList &args, int timeoutMs = 30000);

    static QString ffmpegVersionString();
    static int ffmpegMajorVersion();
    // True when FFmpeg takes "-/filter_complex <file>" (7.0+); older ones need -filter_complex_script.
    static bool ffmpegUsesSlashOptions();
    // The option that reads a filter graph from a file, tested on the actual binary (cached):
    // "-/filter_complex" (FFmpeg 7+) or "-filter_complex_script" (older).
    static QString ffmpegFilterScriptOption();
    static QStringList ffmpegEncoders(); // cached
    static bool hasEncoder(const QString &name);
    static bool encoderWorks(const QString &name); // probes once and caches
    static QString whisperVersionString();
};

} // namespace ss
