#include "TestUtil.h"

#include "core/ProjectSerializer.h"
#include "media/LocalTools.h"
#include "media/MediaProbe.h"

#include <QGuiApplication>
#include <QImage>

using namespace ss;
using namespace ss::test;

class TestTools : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

    QString ff(const QStringList &args, const QString &out)
    {
        const auto r = Tools::run(Tools::ffmpeg(), QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args + QStringList{out}, 120000);
        return r.exitCode == 0 ? out : QString();
    }

private slots:
    void removeRangesRipplesClipAndLinkedAudio()
    {
        Project p = makeEmptyProject("Cut", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 10 * kUsPerSec, true);
        const QString id = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0);
        const int removed = TimelineOps::removeSourceRanges(p, id, {{2 * kUsPerSec, 3 * kUsPerSec}, {6 * kUsPerSec, 7 * kUsPerSec}});
        QCOMPARE(removed, 2);
        QCOMPARE(p.tracks[0].clips.size(), 3);
        QCOMPARE(p.duration(), 8 * kUsPerSec);
        // audio follows exactly
        QCOMPARE(p.tracks[2].clips.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(p.tracks[0].clips[i].start, p.tracks[2].clips[i].start);
            QCOMPARE(p.tracks[0].clips[i].in, p.tracks[2].clips[i].in);
        }
        QCOMPARE(p.tracks[0].clips[1].in, 3 * kUsPerSec);
        QCOMPARE(p.tracks[0].clips[2].in, 7 * kUsPerSec);
        // a range at the very start
        Project q = makeEmptyProject("Cut2", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        q.media << fakeVideo("v", 10 * kUsPerSec, false);
        const QString id2 = TimelineOps::addMediaClip(q, "v", q.tracks[0].id, 0, false);
        QCOMPARE(TimelineOps::removeSourceRanges(q, id2, {{0, kUsPerSec}, {5 * kUsPerSec, 6 * kUsPerSec}}), 2);
        QCOMPARE(q.duration(), 8 * kUsPerSec);
        QCOMPARE(q.tracks[0].clips.first().start, 0);
        QCOMPARE(q.tracks[0].clips.first().in, kUsPerSec);
    }

    void fillerWordsFound()
    {
        Transcript t;
        t.words = {W("So", 0, 300), W("um,", 400, 700), W("today", 800, 1200), W("uh", 1250, 1500), W("we", 1600, 1800)};
        const auto r = TimelineOps::fillerWordRanges(t, TimelineOps::defaultFillerWords());
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].first, 370 * kUsPerMs);
        QCOMPARE(r[0].second, 730 * kUsPerMs);
        QCOMPARE(r[1].first, 1220 * kUsPerMs);
        QCOMPARE(r[1].second, 1530 * kUsPerMs);
    }

    void freezeFrameInsertsAndPushes()
    {
        Project p = makeEmptyProject("Freeze", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 6 * kUsPerSec, true);
        MediaAsset img;
        img.id = "img";
        img.kind = MediaKind::Image;
        img.width = 1920;
        img.height = 1080;
        img.hasVideo = true;
        p.media << img;
        const QString id = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0);
        const QString f = TimelineOps::insertFreezeFrame(p, id, 2 * kUsPerSec, "img", 3 * kUsPerSec);
        QVERIFY(!f.isEmpty());
        QCOMPARE(p.duration(), 9 * kUsPerSec);
        QCOMPARE(p.findClip(f)->start, 2 * kUsPerSec);
        QCOMPARE(p.tracks[0].clips.last().start, 5 * kUsPerSec);
        QCOMPARE(p.tracks[2].clips.last().start, 5 * kUsPerSec); // linked audio moved too
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void freezeFramePushesUnlinkedLaterClips()
    {
        Project p = makeEmptyProject("Freeze2", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 4 * kUsPerSec, false);
        MediaAsset img;
        img.id = "img";
        img.kind = MediaKind::Image;
        img.width = 1920;
        img.height = 1080;
        img.hasVideo = true;
        p.media << img;
        const QString a = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0, false);
        const QString b = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 5 * kUsPerSec, false);
        const QString f = TimelineOps::insertFreezeFrame(p, a, kUsPerSec, "img", 2 * kUsPerSec);
        QVERIFY(!f.isEmpty());
        QCOMPARE(p.findClip(b)->start, 7 * kUsPerSec);
        // nothing overlaps on the track
        const Track &tr = p.tracks[0];
        for (int i = 1; i < tr.clips.size(); ++i)
            QVERIFY(tr.clips[i].start >= tr.clips[i - 1].end());
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void onsetDetectionFindsClicks()
    {
        const int sr = 11025;
        QVector<float> x(sr * 4, 0.0f);
        for (int b = 0; b < 8; ++b)
            for (int k = 0; k < 300; ++k)
                x[b * sr / 2 + 200 + k] = 0.8f * std::exp(-k / 60.0f) * ((k % 2) ? 1 : -1);
        const auto t = LocalTools::detectOnsets(x, sr, 0.6);
        QVERIFY2(t.size() >= 7 && t.size() <= 9, qPrintable(QString::number(t.size())));
        QVERIFY(std::abs(t[1] - t[0] - 0.5) < 0.06);
    }

    void ffmpegToolsWork()
    {
        if (Tools::ffmpeg().isEmpty())
            QSKIP("FFmpeg not available");
        // 2 s red then 2 s blue, with a tone, silence, tone
        const QString v = ff({"-f", "lavfi", "-i", "color=c=red:s=320x180:r=25:d=2", "-f", "lavfi", "-i", "color=c=blue:s=320x180:r=25:d=2",
                              "-f", "lavfi", "-i", "sine=f=440:d=1.5:sample_rate=48000", "-f", "lavfi", "-i", "anullsrc=r=48000:cl=mono:d=1.2",
                              "-f", "lavfi", "-i", "sine=f=660:d=1.3:sample_rate=48000",
                              "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v];[2:a][3:a][4:a]concat=n=3:v=0:a=1[a]",
                              "-map", "[v]", "-map", "[a]", "-c:v", Tools::hasEncoder("libx264") ? "libx264" : "mpeg4", "-pix_fmt", "yuv420p",
                              "-c:a", "aac"},
                             m_tmp.path() + "/rb.mp4");
        QVERIFY(!v.isEmpty());
        QString err;
        const auto sil = LocalTools::silences(v, 0, 4 * kUsPerSec, -40, 500 * kUsPerMs, 50 * kUsPerMs, &err);
        QVERIFY2(sil.size() == 1, qPrintable(err + QString::number(sil.size())));
        QVERIFY(std::abs(sil[0].first - 1550 * kUsPerMs) < 150 * kUsPerMs);
        QVERIFY(std::abs(sil[0].second - 2650 * kUsPerMs) < 150 * kUsPerMs);

        const auto sc = LocalTools::scenes(v, 0, 4 * kUsPerSec, 0.3, &err);
        QVERIFY2(sc.size() == 1, qPrintable(err + QString::number(sc.size())));
        QVERIFY(std::abs(sc[0] - 2 * kUsPerSec) < 100 * kUsPerMs);

        const QString rev = m_tmp.path() + "/rev.mp4";
        const auto r = LocalTools::reverse(v, 500 * kUsPerMs, 3500 * kUsPerMs, true, rev);
        QVERIFY2(r.ok, qPrintable(r.error));
        MediaAsset m;
        QVERIFY(MediaProbe::probe(rev, m, &err));
        QVERIFY(std::abs(m.duration - 3 * kUsPerSec) < 150 * kUsPerMs);
        // reversed: starts blue, ends red
        const auto f1 = LocalTools::extractFrame(rev, 200 * kUsPerMs, m_tmp.path() + "/f1.png");
        const auto f2 = LocalTools::extractFrame(rev, 2800 * kUsPerMs, m_tmp.path() + "/f2.png");
        QVERIFY(f1.ok && f2.ok);
        const QColor c1 = QImage(f1.path).pixelColor(160, 90), c2 = QImage(f2.path).pixelColor(160, 90);
        QVERIFY2(c1.blue() > 150 && c1.red() < 100, qPrintable(c1.name()));
        QVERIFY2(c2.red() > 150 && c2.blue() < 100, qPrintable(c2.name()));

        const auto au = LocalTools::extractAudio(v, 0, 2 * kUsPerSec, m_tmp.path() + "/a.wav");
        QVERIFY2(au.ok, qPrintable(au.error));
        QVERIFY(MediaProbe::probe(au.path, m, &err));
        QVERIFY(m.hasAudio && !m.hasVideo);

        const auto st = LocalTools::stabilize(v, 0, 2 * kUsPerSec, true, m_tmp.path() + "/stable.mp4");
        QVERIFY2(st.ok, qPrintable(st.error));
        QVERIFY(MediaProbe::probe(st.path, m, &err));
        QVERIFY(std::abs(m.duration - 2 * kUsPerSec) < 150 * kUsPerMs);
    }

    void textToSpeechWhenAvailable()
    {
        if (LocalTools::speechEngineName().isEmpty())
            QSKIP("No speech engine on this machine");
        const auto r = LocalTools::textToSpeech("Hello from Shyftr Studio", {}, 1.0, m_tmp.path() + "/tts.wav");
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFileInfo(r.path).size() > 10000);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestTools t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_tools.moc"
