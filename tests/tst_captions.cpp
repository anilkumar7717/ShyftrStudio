#include "TestUtil.h"

#include "captions/CaptionEngine.h"
#include "captions/CaptionStyle.h"
#include "captions/SubtitleIO.h"
#include "core/ProjectSerializer.h"

#include <QtMath>

using namespace ss;
using namespace ss::test;

static const char *kWhisperJson = R"({
  "systeminfo": "test",
  "result": {"language": "hi"},
  "transcription": [
    {"timestamps": {"from": "00:00:00,000", "to": "00:00:02,000"}, "offsets": {"from": 0, "to": 2000},
     "text": " Haan bhai, kaise ho?",
     "tokens": [
       {"text": "[_BEG_]", "offsets": {"from": 0, "to": 0}, "id": 50364, "p": 0.9, "t_dtw": -1},
       {"text": " Ha", "offsets": {"from": 100, "to": 250}, "id": 1, "p": 0.8, "t_dtw": -1},
       {"text": "an", "offsets": {"from": 250, "to": 400}, "id": 2, "p": 0.6, "t_dtw": -1},
       {"text": " bhai", "offsets": {"from": 450, "to": 800}, "id": 3, "p": 0.95, "t_dtw": -1},
       {"text": ",", "offsets": {"from": 800, "to": 820}, "id": 4, "p": 0.99, "t_dtw": -1},
       {"text": " kaise", "offsets": {"from": 900, "to": 1300}, "id": 5, "p": 0.3, "t_dtw": -1},
       {"text": " ho", "offsets": {"from": 1350, "to": 1600}, "id": 6, "p": 0.9, "t_dtw": -1},
       {"text": "?", "offsets": {"from": 1600, "to": 1650}, "id": 7, "p": 0.9, "t_dtw": -1},
       {"text": "[_TT_80]", "offsets": {"from": 1650, "to": 1650}, "id": 50444, "p": 0.5, "t_dtw": -1}
     ]},
    {"timestamps": {"from": "00:00:02,000", "to": "00:00:03,000"}, "offsets": {"from": 2000, "to": 3000},
     "text": " [BLANK_AUDIO]",
     "tokens": [
       {"text": " [", "offsets": {"from": 2000, "to": 2100}, "id": 8, "p": 0.5, "t_dtw": -1},
       {"text": "BLANK_AUDIO", "offsets": {"from": 2100, "to": 2800}, "id": 9, "p": 0.5, "t_dtw": -1},
       {"text": "]", "offsets": {"from": 2800, "to": 3000}, "id": 10, "p": 0.5, "t_dtw": -1}
     ]},
    {"timestamps": {"from": "00:00:03,000", "to": "00:00:04,000"}, "offsets": {"from": 3000, "to": 4000},
     "text": " नमस्ते दोस्तों।",
     "tokens": [
       {"text": " नमस्ते", "offsets": {"from": 3000, "to": 3400}, "id": 11, "p": 0.7, "t_dtw": -1},
       {"text": " दोस्तों", "offsets": {"from": 3450, "to": 3900}, "id": 12, "p": 0.7, "t_dtw": -1},
       {"text": "।", "offsets": {"from": 3900, "to": 3950}, "id": 13, "p": 0.7, "t_dtw": -1}
     ]}
  ]
})";

class TestCaptions : public QObject {
    Q_OBJECT

    QVector<Word> sampleWords()
    {
        return {W("This", 0, 200), W("is", 220, 350), W("a", 360, 420), W("caption", 430, 800), W("test.", 820, 1200),
                W("After", 2000, 2300), W("a", 2320, 2400), W("long", 2410, 2700), W("pause", 2710, 3100),
                W("we", 3120, 3300), W("continue", 3320, 3900)};
    }

private slots:
    void initTestCase()
    {
        static QTemporaryDir tmp;
        AppPaths::setRoot(tmp.path());
        AppPaths::ensureLayout();
    }

    void templateLibraryHasFifteenToFortyTemplates()
    {
        const auto all = TemplateLibrary::instance().builtIns();
        QVERIFY2(all.size() >= 15 && all.size() <= 40, qPrintable(QString::number(all.size())));
        QStringList families;
        for (const auto &s : all) {
            QVERIFY(!s.name.isEmpty());
            if (!families.contains(s.family))
                families << s.family;
        }
        for (const char *f : {"Bold highlight", "One-word pop", "Karaoke fill", "Minimal clean", "Cinematic", "Speaker labels"})
            QVERIFY2(families.contains(f), qPrintable(f));
    }

    void styleOverridesAndUserTemplates()
    {
        CaptionStyle s = TemplateLibrary::instance().get("minimal-clean");
        CaptionStyle o = s.withOverrides(QJsonObject{{"textColor", "#ff00ff00"}, {"maxLines", 1}});
        QCOMPARE(o.textColor, QColor(0, 255, 0));
        QCOMPARE(o.maxLines, 1);
        QCOMPARE(o.fontWeight, s.fontWeight);
        o.name = "My Brand";
        QString err;
        QVERIFY2(TemplateLibrary::instance().saveUserTemplate(o, &err), qPrintable(err));
        TemplateLibrary::instance().reload();
        QVERIFY(TemplateLibrary::instance().contains("user-my-brand"));
        QCOMPARE(TemplateLibrary::instance().get("user-my-brand").textColor, QColor(0, 255, 0));
    }

    void lineBreakerBalancesAndRespectsLimits()
    {
        const QStringList words{"one", "two", "three", "four", "five", "six"};
        auto lines = LineBreaker::breakLines(words, 3, 40, 2);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].size(), 3);
        lines = LineBreaker::breakLines(words, 10, 12, 2);
        for (const auto &l : lines) {
            int chars = 0;
            for (int i : l)
                chars += words[i].size() + 1;
            QVERIFY(chars - 1 <= 12 || l.size() == 1 || lines.size() == 2);
        }
        // overflow folds into last line, never drops text
        lines = LineBreaker::breakLines(words, 1, 40, 2);
        QCOMPARE(lines.size(), 2);
        int total = 0;
        for (const auto &l : lines)
            total += l.size();
        QCOMPARE(total, 6);
    }

    void segmenterBreaksOnPausesSentencesAndLimits()
    {
        SegmentParams p;
        p.maxWordsPerCue = 4;
        p.pauseBreakMs = 450;
        const auto cues = Segmenter::segment(sampleWords(), p);
        QVERIFY(cues.size() >= 3);
        // no cue spans the long pause between "test." and "After"
        for (const Cue &c : cues)
            QVERIFY(!(c.first <= 4 && c.last >= 5));
        for (const Cue &c : cues)
            QVERIFY(c.last - c.first + 1 <= 4);
        // contiguous coverage
        int next = 0;
        for (const Cue &c : cues) {
            QCOMPARE(c.first, next);
            next = c.last + 1;
        }
        QCOMPARE(next, sampleWords().size());
    }

    void segmenterKeepsManualCuesAndHardBreaks()
    {
        QVector<Word> words = sampleWords();
        words[7].hardBreakBefore = true;
        Cue manual{1, 2, -1, {}, true};
        SegmentParams p;
        p.maxWordsPerCue = 10;
        const auto cues = Segmenter::segment(words, p, {manual});
        bool foundManual = false, breakRespected = false;
        for (const Cue &c : cues) {
            if (c.first == 1 && c.last == 2 && c.manual)
                foundManual = true;
            if (c.first == 7)
                breakRespected = true;
        }
        QVERIFY(foundManual);
        QVERIFY(breakRespected);
    }

    void whisperFullJsonToWords()
    {
        QVector<Word> words;
        QString lang, err;
        QVERIFY2(WhisperParser::parseFullJson(kWhisperJson, words, &lang, &err), qPrintable(err));
        QCOMPARE(lang, QString("hi"));
        QStringList texts;
        for (const Word &w : words)
            texts << w.text;
        QCOMPARE(texts, (QStringList{"Haan", "bhai,", "kaise", "ho?", "नमस्ते", "दोस्तों।"}));
        QCOMPARE(words[0].start, TimeUs(100 * kUsPerMs));
        QCOMPARE(words[0].end, TimeUs(400 * kUsPerMs));
        QVERIFY(qAbs(words[0].confidence - 0.7f) < 0.01f);
        QVERIFY(words[2].confidence < 0.5f); // low confidence flagged
        QCOMPARE(WhisperParser::parseProgress("whisper_print_progress_callback: progress =  35%\nprogress = 40%"), 40);
    }

    void alignerSnapsToOnsetsAndTrimsSilence()
    {
        // envelope: silence (-80dB) except speech 500-900ms and 1200-1500ms
        QVector<float> env(300, -80.f);
        for (int f = 50; f < 90; ++f)
            env[f] = -20.f;
        for (int f = 120; f < 150; ++f)
            env[f] = -22.f;
        QVector<Word> words{W("hello", 440, 1100), W("world", 1150, 1700)};
        Aligner::refine(words, env);
        QCOMPARE(words[0].start, TimeUs(500 * kUsPerMs));
        QVERIFY(words[0].end <= TimeUs(900 * kUsPerMs) + 10 * kUsPerMs);
        QCOMPARE(words[1].start, TimeUs(1200 * kUsPerMs));
        QVERIFY(words[1].end <= TimeUs(1500 * kUsPerMs) + 10 * kUsPerMs);
        QVERIFY(words[0].end <= words[1].start);
        const auto regions = Aligner::speechRegions(env);
        QCOMPARE(regions.size(), 2);
    }

    void pcmEnvelope()
    {
        QByteArray pcm(16000 * 2, '\0'); // 1 s of silence @16k
        auto *s = reinterpret_cast<qint16 *>(pcm.data());
        for (int i = 8000; i < 12000; ++i)
            s[i] = qint16(10000 * std::sin(i * 0.1));
        const auto env = Aligner::envelopeFromPcm16(pcm, 16000);
        QCOMPARE(env.size(), 100);
        QVERIFY(env[10] < -90);
        QVERIFY(env[60] > -20);
    }

    void captionsStayAnchoredThroughEdits()
    {
        Project p = makeEmptyProject("A", CanvasSettings());
        p.media << fakeVideo("m1", 10 * kUsPerSec);
        const QString clip = TimelineOps::addMediaClip(p, "m1", {}, 0);
        Transcript t;
        t.id = "tr";
        t.mediaId = "m1";
        t.words = {W("one", 1000, 1400), W("two", 1500, 1900), W("three", 6000, 6400), W("four", 6500, 6900)};
        t.cues = {{0, 1, -1, {}, false}, {2, 3, -1, {}, false}};
        p.transcripts << t;
        Track ct;
        ct.id = "ct";
        ct.kind = TrackKind::Caption;
        ct.transcriptId = "tr";
        p.tracks << ct;

        auto cues = CaptionMapper::map(p, *p.findTrack("ct"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues[0].start, TimeUs(1000 * kUsPerMs));

        // move the clip (and its linked audio) by +2 s -> captions follow
        QVERIFY(TimelineOps::moveClips(p, {clip}, 2 * kUsPerSec));
        cues = CaptionMapper::map(p, *p.findTrack("ct"));
        QCOMPARE(cues[0].start, TimeUs(3000 * kUsPerMs));

        // trim the start past the first cue -> it disappears; second cue keeps its words
        TimelineOps::trimClip(p, clip, TimelineOps::Edge::Start, 2 * kUsPerSec + 3 * kUsPerSec);
        cues = CaptionMapper::map(p, *p.findTrack("ct"));
        QCOMPARE(cues.size(), 1);
        QCOMPARE(cues[0].words.size(), 2);
        QCOMPARE(cues[0].start, TimeUs(8000 * kUsPerMs));

        // split at 8.5 s and delete the right part with ripple -> "four" removed
        Project q = p;
        QVERIFY(TimelineOps::splitClip(q, clip, 8500 * kUsPerMs));
        QString right;
        for (const auto &c : q.tracks[0].clips)
            if (c.start == 8500 * kUsPerMs)
                right = c.id;
        QVERIFY(!right.isEmpty());
        QVERIFY(TimelineOps::deleteClips(q, {right}, true));
        cues = CaptionMapper::map(q, *q.findTrack("ct"));
        QCOMPARE(cues.size(), 1);
        QCOMPARE(cues[0].text(), QString("three"));

        // speed 2x halves caption timing relative to clip start
        Project r = makeEmptyProject("B", CanvasSettings());
        r.media << fakeVideo("m1", 10 * kUsPerSec);
        r.transcripts << t;
        r.tracks << ct;
        const QString c2 = TimelineOps::addMediaClip(r, "m1", {}, 0);
        QVERIFY(TimelineOps::setClipSpeed(r, c2, 2.0));
        cues = CaptionMapper::map(r, *r.findTrack("ct"));
        QCOMPARE(cues[1].start, TimeUs(3000 * kUsPerMs));
    }

    void transcriptEditing()
    {
        Transcript t;
        t.words = sampleWords();
        SegmentParams p;
        p.maxWordsPerCue = 5;
        t.cues = Segmenter::segment(t.words, p);
        const int n = t.cues.size();
        const QString before = t.cueText(0);
        // same word count => timings preserved
        QVERIFY(TranscriptEdit::setCueText(t, 0, "That is the caption test."));
        QCOMPARE(t.words[0].start, TimeUs(0));
        QVERIFY(t.words[0].edited);
        // different word count => redistributed, later cues reindexed
        const QString later = t.cueText(1);
        QVERIFY(TranscriptEdit::setCueText(t, 0, "Completely new text"));
        QCOMPARE(t.cueText(0), QString("Completely new text"));
        QCOMPARE(t.cueText(1), later);
        QVERIFY(ProjectSerializer::validate([&] {
                    Project pr;
                    pr.transcripts << t;
                    pr.transcripts[0].id = "x";
                    return pr;
                }()).isEmpty());
        // split / merge
        QVERIFY(TranscriptEdit::splitCue(t, 0, t.cues[0].first + 1));
        QCOMPARE(t.cues.size(), n + 1);
        QVERIFY(TranscriptEdit::mergeWithNext(t, 0));
        QCOMPARE(t.cues.size(), n);
        // timing edit scales words and clamps to neighbours
        QVERIFY(TranscriptEdit::setCueTiming(t, 1, t.cueStart(1) - 100 * kUsPerMs, t.cueEnd(1) + 100 * kUsPerMs));
        QVERIFY(t.cueStart(1) >= t.cueEnd(0));
        // re-segmenting with a new template keeps user text
        TranscriptEdit::resegment(t, TemplateLibrary::instance().get("one-word-pop"));
        QVERIFY(t.cueText(0).startsWith("Completely"));
        // search & delete
        QCOMPARE(TranscriptEdit::search(t, "PAUSE").size(), 1);
        const int words = t.words.size();
        QVERIFY(TranscriptEdit::deleteCue(t, 0));
        QVERIFY(t.words.size() < words);
        Q_UNUSED(before);
    }

    void subtitleWritersAndReader()
    {
        CaptionStyle s = TemplateLibrary::instance().get("minimal-clean");
        QVector<CueInstance> cues;
        CueInstance a;
        a.start = 1500 * kUsPerMs;
        a.end = 3250 * kUsPerMs;
        a.words = {{0, "Hello", 1500 * kUsPerMs, 2000 * kUsPerMs}, {1, "world", 2100 * kUsPerMs, 3000 * kUsPerMs}};
        cues << a;
        CueInstance b = a;
        b.start = 3600LL * kUsPerSec + 1 * kUsPerMs;
        b.end = b.start + kUsPerSec;
        b.words = {{2, "Later", b.start, b.end}};
        cues << b;
        const QString srt = SubtitleIO::toSrt(cues, s);
        QVERIFY(srt.startsWith("1\n00:00:01,500 --> 00:00:03,250\nHello world\n\n2\n01:00:00,001 --> 01:00:01,001\nLater"));
        const QString vtt = SubtitleIO::toVtt(cues, s, {}, true);
        QVERIFY(vtt.startsWith("WEBVTT\n\n00:00:01.500 --> 00:00:03.250\nHello <00:00:02.100>world"));
        const QString ass = SubtitleIO::toAss(cues, TemplateLibrary::instance().get("karaoke-fill-cyan"), 1920, 1080);
        QVERIFY(ass.contains("PlayResX: 1920"));
        QVERIFY(ass.contains("Dialogue: 0,0:00:01.50,0:00:03.25,Default"));
        QVERIFY(ass.contains("\\kf"));
        QCOMPARE(SubtitleIO::assColor(QColor(255, 0, 0, 255)), QString("&H000000FF"));

        Transcript t;
        QString err;
        QVERIFY2(SubtitleIO::readSubtitles(srt, t, &err), qPrintable(err));
        QCOMPARE(t.cues.size(), 2);
        QCOMPARE(t.cueText(0), QString("Hello world"));
        QCOMPARE(t.cueStart(0), TimeUs(1500 * kUsPerMs));
        QCOMPARE(t.cueEnd(0), TimeUs(3250 * kUsPerMs));
        Transcript v;
        QVERIFY(SubtitleIO::readSubtitles(vtt, v, &err));
        QCOMPARE(v.cueText(0), QString("Hello world"));

        const auto ranged = SubtitleIO::clipRange(cues, 2 * kUsPerSec, 10 * kUsPerSec);
        QCOMPARE(ranged.size(), 1);
        QCOMPARE(ranged[0].start, TimeUs(0));
        QCOMPARE(ranged[0].words.size(), 1); // "Hello" ends exactly at the range start
    }
};

QTEST_GUILESS_MAIN(TestCaptions)
#include "tst_captions.moc"
