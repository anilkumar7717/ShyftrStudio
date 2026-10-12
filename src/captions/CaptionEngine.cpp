#include "CaptionEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <climits>
#include <limits>

namespace ss {

// =================================================================== LineBreaker

QVector<QVector<int>> LineBreaker::breakLines(const QStringList &words, int maxWordsPerLine, int maxCharsPerLine,
                                              int maxLines)
{
    QVector<QVector<int>> lines;
    const int n = words.size();
    if (n == 0)
        return lines;
    maxWordsPerLine = std::max(1, maxWordsPerLine);
    maxCharsPerLine = std::max(1, maxCharsPerLine);
    maxLines = std::max(1, maxLines);

    int totalChars = 0;
    for (const QString &w : words)
        totalChars += w.size();
    totalChars += n - 1;
    int needed = std::max(int(std::ceil(double(n) / maxWordsPerLine)), int(std::ceil(double(totalChars) / maxCharsPerLine)));
    needed = std::clamp(needed, 1, maxLines);
    const double target = double(totalChars) / needed; // balanced line length

    QVector<int> cur;
    int curChars = 0;
    for (int i = 0; i < n; ++i) {
        const int len = words[i].size();
        const int withWord = cur.isEmpty() ? len : curChars + 1 + len;
        bool breakHere = false;
        if (!cur.isEmpty()) {
            if (cur.size() >= maxWordsPerLine || withWord > maxCharsPerLine)
                breakHere = true;
            else if (lines.size() + 1 < needed && curChars >= target * 0.92 && withWord > target)
                breakHere = true; // balance lines
        }
        if (breakHere) {
            lines << cur;
            cur.clear();
            curChars = 0;
        }
        cur << i;
        curChars = cur.size() == 1 ? len : curChars + 1 + len;
    }
    if (!cur.isEmpty())
        lines << cur;
    // Overflow (only possible when a cue is longer than the template allows): fold extra
    // lines into the last allowed line so text is never dropped.
    while (lines.size() > maxLines) {
        QVector<int> extra = lines.takeLast();
        lines.last() += extra;
    }
    return lines;
}

QString LineBreaker::joinLines(const QStringList &words, const QVector<QVector<int>> &lines, const QString &sep)
{
    QStringList out;
    for (const auto &l : lines) {
        QStringList parts;
        for (int i : l)
            parts << words[i];
        out << parts.join(' ');
    }
    return out.join(sep);
}

// =================================================================== Segmenter

SegmentParams SegmentParams::fromStyle(const CaptionStyle &s)
{
    SegmentParams p;
    p.maxWordsPerCue = s.maxWordsPerCue();
    p.maxCharsPerCue = s.maxCharsPerLine * s.maxLines;
    p.pauseBreakMs = s.pauseBreakMs;
    p.maxCueMs = s.maxCueMs;
    p.minCueMs = s.minCueMs;
    return p;
}

bool Segmenter::isSentenceEnd(const QString &word)
{
    if (word.isEmpty())
        return false;
    const QChar c = word.back();
    // Latin punctuation plus Devanagari danda (Hindi) and Gurmukhi usage of the same marks.
    return c == '.' || c == '?' || c == '!' || c == QChar(0x0964) || c == QChar(0x0965) || c == QChar(0x3002);
}

static void segmentRun(const QVector<Word> &words, int from, int to, const SegmentParams &p, QVector<Cue> &out)
{
    int i = from;
    while (i <= to) {
        Cue c;
        c.first = i;
        c.last = i;
        int chars = words[i].text.size();
        int j = i + 1;
        while (j <= to) {
            const Word &w = words[j];
            const Word &prev = words[j - 1];
            const TimeUs gap = w.start - prev.end;
            const TimeUs dur = w.end - words[c.first].start;
            const int count = j - c.first;
            const int newChars = chars + 1 + w.text.size();
            if (w.hardBreakBefore)
                break;
            if (count >= p.maxWordsPerCue || newChars > p.maxCharsPerCue)
                break;
            if (dur > TimeUs(p.maxCueMs) * kUsPerMs)
                break;
            if (gap >= TimeUs(p.pauseBreakMs) * kUsPerMs)
                break;
            if (Segmenter::isSentenceEnd(prev.text)) {
                const TimeUs curDur = prev.end - words[c.first].start;
                if (curDur >= TimeUs(p.minCueMs) * kUsPerMs || count >= 2)
                    break;
            }
            chars = newChars;
            c.last = j;
            ++j;
        }
        out << c;
        i = c.last + 1;
    }
}

QVector<Cue> Segmenter::segment(const QVector<Word> &words, const SegmentParams &p, const QVector<Cue> &existing)
{
    QVector<Cue> manual;
    for (const Cue &c : existing)
        if (c.manual && c.first >= 0 && c.last < words.size() && c.first <= c.last)
            manual << c;
    std::sort(manual.begin(), manual.end(), [](const Cue &a, const Cue &b) { return a.first < b.first; });
    // drop overlapping manual cues
    QVector<Cue> cleanManual;
    int lastEnd = -1;
    for (const Cue &c : manual)
        if (c.first > lastEnd) {
            cleanManual << c;
            lastEnd = c.last;
        }

    // carry speaker/style info for non-manual cues by first word
    QHash<int, Cue> carried;
    for (const Cue &c : existing)
        if (!c.manual)
            for (int w = c.first; w <= c.last; ++w)
                if (!carried.contains(w))
                    carried[w] = c;

    QVector<Cue> out;
    int pos = 0;
    for (const Cue &m : cleanManual) {
        if (m.first > pos)
            segmentRun(words, pos, m.first - 1, p, out);
        out << m;
        pos = m.last + 1;
    }
    if (pos < words.size())
        segmentRun(words, pos, words.size() - 1, p, out);

    for (Cue &c : out)
        if (!c.manual && carried.contains(c.first)) {
            c.speaker = carried[c.first].speaker;
            c.styleOverrides = carried[c.first].styleOverrides;
        }
    std::sort(out.begin(), out.end(), [](const Cue &a, const Cue &b) { return a.first < b.first; });
    return out;
}

// =================================================================== Aligner

QVector<float> Aligner::envelopeFromPcm16(const QByteArray &pcm, int sampleRate, int frameMs)
{
    QVector<float> env;
    const int frameLen = std::max(1, sampleRate * frameMs / 1000);
    const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
    const int n = pcm.size() / 2;
    env.reserve(n / frameLen + 1);
    for (int i = 0; i < n; i += frameLen) {
        const int end = std::min(n, i + frameLen);
        double acc = 0;
        for (int k = i; k < end; ++k)
            acc += double(s[k]) * s[k];
        const double rms = std::sqrt(acc / std::max(1, end - i)) / 32768.0;
        env << float(rms > 1e-5 ? 20.0 * std::log10(rms) : -100.0);
    }
    return env;
}

static float noiseFloor(const QVector<float> &env)
{
    if (env.isEmpty())
        return -100.f;
    QVector<float> sorted = env;
    std::sort(sorted.begin(), sorted.end());
    return sorted[int(sorted.size() * 0.15)];
}

static float speechThreshold(const QVector<float> &env, double overFloorDb)
{
    const float floor = noiseFloor(env);
    return std::max(float(floor + overFloorDb), -60.f);
}

void Aligner::refine(QVector<Word> &words, const QVector<float> &env, const Params &p)
{
    if (env.isEmpty() || words.isEmpty())
        return;
    const float thr = speechThreshold(env, p.thresholdOverFloorDb);
    const int nF = env.size();
    auto frameOf = [](TimeUs t) { return int(t / (10 * kUsPerMs)); };
    auto timeOf = [](int f) { return TimeUs(f) * 10 * kUsPerMs; };
    auto loud = [&](int f) { return f >= 0 && f < nF && env[f] >= thr; };

    for (Word &w : words) {
        const int sF = frameOf(w.start);
        const int eF = frameOf(w.end);
        // --- start: nearest onset (quiet -> loud transition) in the window
        int bestOnset = -1;
        int bestDist = INT_MAX;
        const int lo = std::max(0, sF - p.searchBeforeMs / 10);
        const int hi = std::min(nF - 1, sF + p.searchAfterMs / 10);
        for (int f = lo; f <= hi; ++f)
            if (loud(f) && !loud(f - 1)) {
                const int d = std::abs(f - sF);
                if (d < bestDist) {
                    bestDist = d;
                    bestOnset = f;
                }
            }
        if (bestOnset >= 0 && bestOnset < eF)
            w.start = timeOf(bestOnset);
        // --- end: trim trailing silence (bounded)
        int f = std::min(eF, nF) - 1;
        const int stop = std::max(frameOf(w.start), eF - p.maxTrailingSilenceMs / 10);
        while (f > stop && !loud(f))
            --f;
        if (f > frameOf(w.start) && f < eF - 1)
            w.end = timeOf(f + 1);
    }
    // --- monotonic, non-overlapping, minimum duration
    const TimeUs minLen = TimeUs(p.minWordMs) * kUsPerMs;
    for (int i = 0; i < words.size(); ++i) {
        Word &w = words[i];
        if (i > 0 && w.start < words[i - 1].end) {
            // split the overlap evenly
            const TimeUs mid = (w.start + words[i - 1].end) / 2;
            words[i - 1].end = std::max(words[i - 1].start + 1, mid);
            w.start = words[i - 1].end;
        }
        if (w.end - w.start < minLen) {
            TimeUs limit = i + 1 < words.size() ? std::max(words[i + 1].start, w.start + 1) : w.start + minLen;
            w.end = std::min(w.start + minLen, std::max(limit, w.end));
            if (w.end <= w.start)
                w.end = w.start + 1;
        }
    }
}

QVector<QPair<TimeUs, TimeUs>> Aligner::speechRegions(const QVector<float> &env, int minSpeechMs, int minSilenceMs)
{
    QVector<QPair<TimeUs, TimeUs>> regions;
    if (env.isEmpty())
        return regions;
    const float thr = speechThreshold(env, 9.0);
    int start = -1;
    for (int f = 0; f <= env.size(); ++f) {
        const bool l = f < env.size() && env[f] >= thr;
        if (l && start < 0)
            start = f;
        if (!l && start >= 0) {
            regions << qMakePair(TimeUs(start) * 10 * kUsPerMs, TimeUs(f) * 10 * kUsPerMs);
            start = -1;
        }
    }
    QVector<QPair<TimeUs, TimeUs>> merged;
    for (const auto &r : regions) {
        if (!merged.isEmpty() && r.first - merged.last().second < TimeUs(minSilenceMs) * kUsPerMs)
            merged.last().second = r.second;
        else
            merged << r;
    }
    QVector<QPair<TimeUs, TimeUs>> out;
    for (const auto &r : merged)
        if (r.second - r.first >= TimeUs(minSpeechMs) * kUsPerMs)
            out << r;
    return out;
}

// =================================================================== WhisperParser

static bool isNonSpeechMarker(const QString &w)
{
    static const QRegularExpression re(R"(^\s*[\[\(\*♪].*[\]\)\*♪]\s*$)");
    return w.trimmed().isEmpty() || re.match(w).hasMatch();
}

bool WhisperParser::parseFullJson(const QByteArray &jsonIn, QVector<Word> &words, QString *lang, QString *error)
{
    QByteArray json = jsonIn;
    for (char &ch : json)
        if (static_cast<unsigned char>(ch) < 0x20 && ch != '\n' && ch != '\r' && ch != '\t')
            ch = ' ';
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = "Could not read the transcription result: " + pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    if (lang)
        *lang = root.value("result").toObject().value("language").toString();
    words.clear();

    for (const QJsonValue &sv : root.value("transcription").toArray()) {
        const QJsonObject seg = sv.toObject();
        const QJsonObject segOff = seg.value("offsets").toObject();
        const TimeUs segFrom = segOff.value("from").toInteger() * kUsPerMs;
        const TimeUs segTo = segOff.value("to").toInteger() * kUsPerMs;
        const QJsonArray tokens = seg.value("tokens").toArray();

        if (tokens.isEmpty()) {
            // No token detail: distribute the segment time over its words by length.
            const QStringList parts = seg.value("text").toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            int total = 0;
            for (const QString &p : parts)
                total += p.size() + 1;
            TimeUs t = segFrom;
            for (const QString &p : parts) {
                if (isNonSpeechMarker(p))
                    continue;
                const TimeUs d = total ? (segTo - segFrom) * (p.size() + 1) / total : 0;
                words << Word{p, t, t + d, -1.f, false, false};
                t += d;
            }
            continue;
        }

        Word cur;
        bool have = false;
        double pSum = 0;
        int pCount = 0;
        auto flush = [&]() {
            if (have) {
                cur.text = cur.text.trimmed();
                if (!cur.text.isEmpty() && !isNonSpeechMarker(cur.text)) {
                    cur.confidence = pCount ? float(pSum / pCount) : -1.f;
                    if (cur.end < cur.start)
                        cur.end = cur.start;
                    words << cur;
                }
            }
            cur = Word();
            have = false;
            pSum = 0;
            pCount = 0;
        };

        for (const QJsonValue &tv : tokens) {
            const QJsonObject tok = tv.toObject();
            const QString text = tok.value("text").toString();
            if (text.startsWith("[_") || text.startsWith("<|"))
                continue; // special tokens (timestamps, begin markers)
            TimeUs t0 = segFrom, t1 = segTo;
            if (tok.contains("offsets")) {
                const QJsonObject off = tok.value("offsets").toObject();
                t0 = off.value("from").toInteger() * kUsPerMs;
                t1 = off.value("to").toInteger() * kUsPerMs;
            }
            const bool startsWord = text.startsWith(' ') || !have;
            // punctuation-only tokens attach to the previous word
            static const QRegularExpression punct(R"(^[\p{P}]+$)");
            const bool isPunct = punct.match(text.trimmed()).hasMatch();
            if (startsWord && !(isPunct && have)) {
                flush();
                cur.text = text;
                cur.start = t0;
                cur.end = t1;
                have = true;
            } else {
                cur.text += text;
                cur.end = std::max(cur.end, t1);
            }
            if (tok.contains("p")) {
                pSum += tok.value("p").toDouble();
                ++pCount;
            }
        }
        flush();
    }
    // Ensure ordering
    std::stable_sort(words.begin(), words.end(), [](const Word &a, const Word &b) { return a.start < b.start; });
    return true;
}

int WhisperParser::parseProgress(const QByteArray &chunk)
{
    static const QRegularExpression re(R"(progress\s*=\s*(\d+)\s*%)");
    int last = -1;
    auto it = re.globalMatch(QString::fromUtf8(chunk));
    while (it.hasNext())
        last = it.next().captured(1).toInt();
    return last;
}

// =================================================================== CaptionMapper

QString CueInstance::text() const
{
    QStringList parts;
    for (const auto &w : words)
        parts << w.text;
    return parts.join(' ');
}

QVector<const Clip *> CaptionMapper::anchorClips(const Project &p, const QString &mediaId)
{
    QVector<const Clip *> audio, video;
    QSet<QString> seenLinksA, seenLinksV;
    for (const auto &t : p.tracks) {
        if (t.kind == TrackKind::Caption)
            continue;
        for (const auto &c : t.clips) {
            if (c.type != ClipType::Media || c.mediaId != mediaId || !c.enabled)
                continue;
            if (t.kind == TrackKind::Audio) {
                if (!c.linkId.isEmpty() && seenLinksA.contains(c.linkId))
                    continue;
                seenLinksA.insert(c.linkId);
                audio << &c;
            } else {
                if (!c.linkId.isEmpty() && seenLinksV.contains(c.linkId))
                    continue;
                seenLinksV.insert(c.linkId);
                video << &c;
            }
        }
    }
    // Captions follow the audio when it exists (speech lives there); otherwise the video.
    QVector<const Clip *> out = audio.isEmpty() ? video : audio;
    std::sort(out.begin(), out.end(), [](const Clip *a, const Clip *b) { return a->start < b->start; });
    return out;
}

static void finishInstances(QVector<CueInstance> &out)
{
    std::sort(out.begin(), out.end(), [](const CueInstance &a, const CueInstance &b) { return a.start < b.start; });
    // readability hold: keep a caption on screen a little longer if there's room
    const TimeUs hold = 250 * kUsPerMs;
    for (int i = 0; i < out.size(); ++i) {
        const TimeUs nextStart = i + 1 < out.size() ? out[i + 1].start : out[i].end + hold;
        out[i].end = std::max(out[i].end, std::min(out[i].end + hold, nextStart));
        if (i + 1 < out.size() && out[i].end > out[i + 1].start)
            out[i].end = out[i + 1].start;
    }
}

QVector<CueInstance> CaptionMapper::mapTranscript(const Project &p, const Transcript &t)
{
    QVector<CueInstance> out;
    if (t.mediaId.isEmpty()) {
        for (int ci = 0; ci < t.cues.size(); ++ci) {
            const Cue &c = t.cues[ci];
            CueInstance inst;
            inst.cueIndex = ci;
            inst.speaker = c.speaker;
            inst.overrides = c.styleOverrides;
            for (int w = c.first; w <= c.last && w < t.words.size(); ++w)
                inst.words << WordInstance{w, t.words[w].text, t.words[w].start, t.words[w].end};
            if (inst.words.isEmpty())
                continue;
            inst.start = inst.words.first().start;
            inst.end = inst.words.last().end;
            out << inst;
        }
        finishInstances(out);
        return out;
    }

    const auto clips = anchorClips(p, t.mediaId);
    for (const Clip *clip : clips) {
        for (int ci = 0; ci < t.cues.size(); ++ci) {
            const Cue &c = t.cues[ci];
            if (c.first >= t.words.size())
                continue;
            if (t.words[c.first].start >= clip->out || t.words[std::min(c.last, int(t.words.size()) - 1)].end <= clip->in)
                continue;
            CueInstance inst;
            inst.cueIndex = ci;
            inst.speaker = c.speaker;
            inst.overrides = c.styleOverrides;
            for (int w = c.first; w <= c.last && w < t.words.size(); ++w) {
                const Word &word = t.words[w];
                const TimeUs ws = std::max(word.start, clip->in);
                const TimeUs we = std::min(word.end, clip->out);
                const TimeUs len = std::max<TimeUs>(1, word.end - word.start);
                if (we <= ws || (we - ws) * 2 < len)
                    continue; // word was trimmed away
                inst.words << WordInstance{w, word.text, clip->sourceToTimeline(ws), clip->sourceToTimeline(we)};
            }
            if (inst.words.isEmpty())
                continue;
            inst.start = inst.words.first().start;
            inst.end = std::min(inst.words.last().end, clip->end());
            out << inst;
        }
    }
    finishInstances(out);
    return out;
}

QVector<CueInstance> CaptionMapper::map(const Project &p, const Track &captionTrack)
{
    const Transcript *t = p.findTranscript(captionTrack.transcriptId);
    if (!t)
        return {};
    return mapTranscript(p, *t);
}

bool CaptionMapper::timelineToTranscript(const Project &p, const Transcript &t, TimeUs timeline, TimeUs *out)
{
    if (t.mediaId.isEmpty()) {
        *out = timeline;
        return true;
    }
    for (const Clip *c : anchorClips(p, t.mediaId))
        if (timeline >= c->start && timeline <= c->end()) {
            *out = c->timelineToSource(timeline);
            return true;
        }
    return false;
}

const CueInstance *CaptionMapper::instanceAt(const QVector<CueInstance> &cues, TimeUs t)
{
    // binary search on start
    int lo = 0, hi = cues.size() - 1, found = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (cues[mid].start <= t) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (found >= 0 && t < cues[found].end)
        return &cues[found];
    return nullptr;
}

// =================================================================== TranscriptEdit

static void removeWords(Transcript &t, int first, int last)
{
    const int n = last - first + 1;
    t.words.remove(first, n);
    for (int i = t.cues.size() - 1; i >= 0; --i) {
        Cue &c = t.cues[i];
        if (c.first >= first && c.last <= last) {
            t.cues.removeAt(i);
            continue;
        }
        if (c.first > last) {
            c.first -= n;
            c.last -= n;
        }
    }
}

bool TranscriptEdit::setCueText(Transcript &t, int ci, const QString &text)
{
    if (ci < 0 || ci >= t.cues.size())
        return false;
    const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return deleteCue(t, ci);
    Cue &c = t.cues[ci];
    const int oldCount = c.last - c.first + 1;
    if (parts.size() == oldCount) {
        bool changed = false;
        for (int i = 0; i < oldCount; ++i) {
            Word &w = t.words[c.first + i];
            if (w.text != parts[i]) {
                w.text = parts[i];
                w.edited = true;
                changed = true;
            }
        }
        return changed;
    }
    const TimeUs s = t.words[c.first].start;
    const TimeUs e = t.words[c.last].end;
    int totalWeight = 0;
    for (const QString &p : parts)
        totalWeight += p.size() + 1;
    QVector<Word> nw;
    TimeUs cur = s;
    for (int i = 0; i < parts.size(); ++i) {
        Word w;
        w.text = parts[i];
        w.start = cur;
        w.end = i == parts.size() - 1 ? e : cur + (e - s) * (parts[i].size() + 1) / totalWeight;
        w.edited = true;
        w.hardBreakBefore = i == 0 && t.words[c.first].hardBreakBefore;
        cur = w.end;
        nw << w;
    }
    const int delta = parts.size() - oldCount;
    t.words.remove(c.first, oldCount);
    for (int i = 0; i < nw.size(); ++i)
        t.words.insert(c.first + i, nw[i]);
    c.last = c.first + nw.size() - 1;
    for (int i = ci + 1; i < t.cues.size(); ++i) {
        t.cues[i].first += delta;
        t.cues[i].last += delta;
    }
    return true;
}

bool TranscriptEdit::setWordText(Transcript &t, int wi, const QString &text)
{
    if (wi < 0 || wi >= t.words.size() || text.trimmed().isEmpty())
        return false;
    if (t.words[wi].text == text.trimmed())
        return false;
    t.words[wi].text = text.trimmed();
    t.words[wi].edited = true;
    return true;
}

bool TranscriptEdit::setCueTiming(Transcript &t, int ci, TimeUs start, TimeUs end)
{
    if (ci < 0 || ci >= t.cues.size() || end <= start)
        return false;
    Cue &c = t.cues[ci];
    const TimeUs prevEnd = ci > 0 ? t.words[t.cues[ci - 1].last].end : 0;
    const TimeUs nextStart = ci + 1 < t.cues.size() ? t.words[t.cues[ci + 1].first].start : std::numeric_limits<TimeUs>::max();
    start = std::max(start, prevEnd);
    end = std::min(end, nextStart);
    if (end <= start)
        return false;
    const TimeUs os = t.words[c.first].start;
    const TimeUs oe = t.words[c.last].end;
    const double scale = oe > os ? double(end - start) / double(oe - os) : 1.0;
    for (int i = c.first; i <= c.last; ++i) {
        Word &w = t.words[i];
        w.start = start + TimeUs(std::llround((w.start - os) * scale));
        w.end = start + TimeUs(std::llround((w.end - os) * scale));
    }
    t.words[c.last].end = end;
    c.manual = true;
    return true;
}

bool TranscriptEdit::setWordTiming(Transcript &t, int wi, TimeUs start, TimeUs end)
{
    if (wi < 0 || wi >= t.words.size() || end <= start)
        return false;
    const TimeUs lo = wi > 0 ? t.words[wi - 1].end : 0;
    const TimeUs hi = wi + 1 < t.words.size() ? t.words[wi + 1].start : std::numeric_limits<TimeUs>::max();
    start = std::max(start, lo);
    end = std::min(end, hi);
    if (end <= start)
        return false;
    t.words[wi].start = start;
    t.words[wi].end = end;
    t.words[wi].edited = true;
    return true;
}

bool TranscriptEdit::splitCue(Transcript &t, int ci, int at)
{
    if (ci < 0 || ci >= t.cues.size())
        return false;
    Cue c = t.cues[ci];
    if (at <= c.first || at > c.last)
        return false;
    Cue a = c, b = c;
    a.last = at - 1;
    b.first = at;
    a.manual = b.manual = true;
    t.words[at].hardBreakBefore = true;
    t.cues[ci] = a;
    t.cues.insert(ci + 1, b);
    return true;
}

bool TranscriptEdit::mergeWithNext(Transcript &t, int ci)
{
    if (ci < 0 || ci + 1 >= t.cues.size())
        return false;
    t.words[t.cues[ci + 1].first].hardBreakBefore = false;
    t.cues[ci].last = t.cues[ci + 1].last;
    t.cues[ci].manual = true;
    t.cues.removeAt(ci + 1);
    return true;
}

bool TranscriptEdit::deleteCue(Transcript &t, int ci)
{
    if (ci < 0 || ci >= t.cues.size())
        return false;
    removeWords(t, t.cues[ci].first, t.cues[ci].last);
    return true;
}

bool TranscriptEdit::setCueSpeaker(Transcript &t, int ci, int speaker)
{
    if (ci < 0 || ci >= t.cues.size() || t.cues[ci].speaker == speaker)
        return false;
    t.cues[ci].speaker = speaker;
    return true;
}

bool TranscriptEdit::shiftAll(Transcript &t, TimeUs delta)
{
    if (delta == 0 || t.words.isEmpty())
        return false;
    if (t.words.first().start + delta < 0)
        delta = -t.words.first().start;
    for (Word &w : t.words) {
        w.start += delta;
        w.end += delta;
    }
    return true;
}

void TranscriptEdit::resegment(Transcript &t, const CaptionStyle &style)
{
    t.cues = Segmenter::segment(t.words, SegmentParams::fromStyle(style), t.cues);
}

QVector<int> TranscriptEdit::search(const Transcript &t, const QString &query)
{
    QVector<int> out;
    if (query.trimmed().isEmpty())
        return out;
    for (int i = 0; i < t.cues.size(); ++i)
        if (t.cueText(i).contains(query, Qt::CaseInsensitive))
            out << i;
    return out;
}

} // namespace ss
