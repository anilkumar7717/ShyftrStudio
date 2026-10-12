#pragma once

#include "../core/Model.h"
#include "CaptionEngine.h"
#include "CaptionStyle.h"

namespace ss {

namespace SubtitleIO {
// Writers take timeline-mapped cues, so sidecar files match the edited timeline exactly.
QString toSrt(const QVector<CueInstance> &cues, const CaptionStyle &style, TimeUs offset = 0);
QString toVtt(const QVector<CueInstance> &cues, const CaptionStyle &style, const QVector<Speaker> &speakers = {},
              bool wordTimestamps = false, TimeUs offset = 0);
// Styled interchange (libass-compatible). Karaoke/highlight templates emit \k timing.
QString toAss(const QVector<CueInstance> &cues, const CaptionStyle &style, int width, int height, TimeUs offset = 0);
QString toPlainText(const QVector<CueInstance> &cues, bool withTimes);

// Restricts cues to [from, to) and shifts them so `from` becomes 0 (range exports).
QVector<CueInstance> clipRange(const QVector<CueInstance> &cues, TimeUs from, TimeUs to);

// Reads .srt / .vtt into a timeline-domain transcript (one manual cue per subtitle block).
bool readSubtitles(const QString &text, Transcript &out, QString *error);

bool writeTextFile(const QString &path, const QString &text, QString *error);

QString srtTime(TimeUs t);
QString vttTime(TimeUs t);
QString assTime(TimeUs t);
QString assColor(const QColor &c);
} // namespace SubtitleIO

} // namespace ss
