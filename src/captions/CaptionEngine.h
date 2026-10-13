#pragma once

#include "../core/Model.h"
#include "CaptionStyle.h"

#include <QJsonObject>
#include <QVector>

namespace ss {

// ------------------------------------------------------------------ line breaking
// Shared by preview, burn-in export and SRT/VTT/ASS output, so every output breaks
// lines identically.
namespace LineBreaker {
// Returns line groups as word indices (0-based within the given list).
QVector<QVector<int>> breakLines(const QStringList &words, int maxWordsPerLine, int maxCharsPerLine, int maxLines);
QString joinLines(const QStringList &words, const QVector<QVector<int>> &lines, const QString &sep = "\n");
} // namespace LineBreaker

// ------------------------------------------------------------------ segmentation
struct SegmentParams {
    int maxWordsPerCue = 8;
    int maxCharsPerCue = 64;
    int pauseBreakMs = 450;
    int maxCueMs = 5000;
    int minCueMs = 700;
    static SegmentParams fromStyle(const CaptionStyle &s);
};

namespace Segmenter {
// Builds cues from words. Manual cues in `existing` are kept intact; words they cover
// are not re-grouped. Words flagged hardBreakBefore always start a new cue.
QVector<Cue> segment(const QVector<Word> &words, const SegmentParams &p, const QVector<Cue> &existing = {});
bool isSentenceEnd(const QString &word);
} // namespace Segmenter

// ------------------------------------------------------------------ alignment refinement
// Energy-based refinement of ASR word timings: snaps word starts to nearby speech onsets,
// trims trailing silence, enforces monotonic, non-overlapping, minimum-length words.
// This is a heuristic local aligner; it is an interface boundary where a dedicated
// forced-alignment model can be plugged in later.
namespace Aligner {
struct Params {
    int searchBeforeMs = 120;
    int searchAfterMs = 100;
    int minWordMs = 60;
    int maxTrailingSilenceMs = 250;
    double thresholdOverFloorDb = 9.0;
};
// envelopeDb: RMS level in dBFS per 10 ms frame.
void refine(QVector<Word> &words, const QVector<float> &envelopeDb, const Params &p = Params());
QVector<float> envelopeFromPcm16(const QByteArray &pcm, int sampleRate, int frameMs = 10);
// Simple voice activity detection on the envelope -> speech regions [start,end) in µs.
QVector<QPair<TimeUs, TimeUs>> speechRegions(const QVector<float> &envelopeDb, int minSpeechMs = 200,
                                             int minSilenceMs = 300);
} // namespace Aligner

// ------------------------------------------------------------------ whisper.cpp output
namespace WhisperParser {
// Parses whisper.cpp --output-json-full into words (µs, relative to the audio start).
bool parseFullJson(const QByteArray &json, QVector<Word> &words, QString *detectedLanguage, QString *error);
// Parses "progress = NN%" lines from stderr; returns -1 if none.
int parseProgress(const QByteArray &stderrChunk);
} // namespace WhisperParser

// ------------------------------------------------------------------ timeline mapping
struct WordInstance {
    int wordIndex = -1;
    QString text;
    TimeUs start = 0; // timeline time
    TimeUs end = 0;
};

struct CueInstance {
    int cueIndex = -1;
    TimeUs start = 0; // timeline time
    TimeUs end = 0;
    int speaker = -1;
    QVector<WordInstance> words;
    QJsonObject overrides;
    QString text() const;
};

namespace CaptionMapper {
// Maps a caption track's transcript (source time) through every timeline clip that uses
// the bound media, so captions stay attached to the spoken words when clips are trimmed,
// split, moved, sped up or deleted.
QVector<CueInstance> map(const Project &p, const Track &captionTrack);
QVector<CueInstance> mapTranscript(const Project &p, const Transcript &t);
// Clips used for anchoring (deduplicated linked pairs).
QVector<const Clip *> anchorClips(const Project &p, const QString &mediaId);
// Converts a timeline time to transcript time using the anchor clip under t.
bool timelineToTranscript(const Project &p, const Transcript &t, TimeUs timeline, TimeUs *out);
const CueInstance *instanceAt(const QVector<CueInstance> &cues, TimeUs t);
} // namespace CaptionMapper

// ------------------------------------------------------------------ transcript editing
namespace TranscriptEdit {
// Replaces the text of a cue. Word timings are kept when the word count matches;
// otherwise the cue span is redistributed proportionally to word length.
bool setCueText(Transcript &t, int cueIndex, const QString &text);
bool setWordText(Transcript &t, int wordIndex, const QString &text);
bool setCueTiming(Transcript &t, int cueIndex, TimeUs start, TimeUs end); // scales its words
// Shifts a whole caption (its words keep their spacing) by delta, clamped between its
// neighbours. Returns false if it could not move at all.
bool moveCue(Transcript &t, int cueIndex, TimeUs delta);
bool setWordTiming(Transcript &t, int wordIndex, TimeUs start, TimeUs end);
bool splitCue(Transcript &t, int cueIndex, int atWordIndex);   // new cue starts at atWordIndex
bool mergeWithNext(Transcript &t, int cueIndex);
bool deleteCue(Transcript &t, int cueIndex);
bool setCueSpeaker(Transcript &t, int cueIndex, int speaker);
bool shiftAll(Transcript &t, TimeUs delta);
// Re-segments the whole transcript (keeps manual cues and user text corrections).
void resegment(Transcript &t, const CaptionStyle &style);
QVector<int> search(const Transcript &t, const QString &query); // cue indices
} // namespace TranscriptEdit

} // namespace ss
