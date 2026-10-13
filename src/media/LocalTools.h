#pragma once

// Heavier one-off operations, all done on this computer with FFmpeg (and the operating
// system's own speech voices for text-to-speech). Each returns plain data; the caller
// applies the result to the project inside an undoable Document::apply().

#include "../core/Model.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace ss {
namespace LocalTools {

using Progress = std::function<bool(double fraction)>; // return false to cancel

struct Result {
    bool ok = false;
    QString error;
    QString path;
};

// Renders [in, out) of a file playing backwards (video and sound), in chunks so long clips
// never need to fit in memory.
Result reverse(const QString &src, TimeUs in, TimeUs out, bool hasAudio, const QString &dst, const Progress &progress = {});
// Stabilises shaky footage (vid.stab two-pass when FFmpeg has it, otherwise deshake).
Result stabilize(const QString &src, TimeUs in, TimeUs out, bool hasAudio, const QString &dst, const Progress &progress = {});
// Saves the sound of [in, out) as a file (.m4a, .wav or .mp3 by extension).
Result extractAudio(const QString &src, TimeUs in, TimeUs out, const QString &dst);
// Saves one frame as PNG.
Result extractFrame(const QString &src, TimeUs at, const QString &dstPng);

// Quiet parts inside [in, out) in source time (already shrunk by `padding` on each side).
QVector<QPair<TimeUs, TimeUs>> silences(const QString &src, TimeUs in, TimeUs out, double thresholdDb, TimeUs minDuration,
                                        TimeUs padding, QString *error = nullptr);
// Scene cuts inside [in, out) in source time.
QVector<TimeUs> scenes(const QString &src, TimeUs in, TimeUs out, double threshold, QString *error = nullptr);
// Musical beats / onsets inside [in, out) in source time. sensitivity 0..1.
QVector<TimeUs> beats(const QString &src, TimeUs in, TimeUs out, double sensitivity, QString *error = nullptr);
// Onset detection on mono PCM (exposed for tests).
QVector<double> detectOnsets(const QVector<float> &mono, int sampleRate, double sensitivity);

// Text to speech with the voices installed on this computer (macOS say, Windows SAPI,
// Linux espeak-ng). Writes a 48 kHz WAV.
QStringList voices();
Result textToSpeech(const QString &text, const QString &voice, double rate, const QString &dstWav);
QString speechEngineName();

bool ffmpegHasFilter(const QString &name);

} // namespace LocalTools
} // namespace ss
