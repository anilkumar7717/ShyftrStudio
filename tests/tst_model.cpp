#include "TestUtil.h"

using namespace ss;

class TestModel : public QObject {
    Q_OBJECT
private slots:
    void rationalParsing()
    {
        QCOMPARE(Rational::fromString("30000/1001").num, 30000);
        QCOMPARE(Rational::fromString("30000/1001").den, 1001);
        QCOMPARE(Rational::fromDouble(29.97).num, 30000);
        QCOMPARE(Rational::fromDouble(25).num, 25);
        QCOMPARE(Rational::fromString("garbage", {24, 1}).num, 24);
        QVERIFY(Rational({60, 2}) == Rational({30, 1}));
    }

    void frameMathIsExactForNtsc()
    {
        const Rational ntsc{30000, 1001};
        // 1 hour of frames maps back to the same frame index (no drift)
        for (qint64 f : {0LL, 1LL, 29LL, 30LL, 1799LL, 107892LL}) {
            const TimeUs t = framesToUs(f, ntsc);
            QCOMPARE(usToFrameFloor(t, ntsc), f);
            QCOMPARE(usToFrameRound(t, ntsc), f);
        }
        QCOMPARE(framesToUs(30000, ntsc), TimeUs(1001) * kUsPerSec);
    }

    void snapping()
    {
        const Rational fps{25, 1};
        QCOMPARE(snapToFrame(41 * kUsPerMs, fps), TimeUs(40 * kUsPerMs));
        QCOMPARE(snapToFrame(59 * kUsPerMs, fps), TimeUs(40 * kUsPerMs));
        QCOMPARE(snapToFrame(61 * kUsPerMs, fps), TimeUs(80 * kUsPerMs));
    }

    void timecodeFormatting()
    {
        QCOMPARE(formatTimecode(framesToUs(30 * 61 + 5, {30, 1}), {30, 1}), QString("00:01:01:05"));
        QCOMPARE(formatClock(3723 * kUsPerSec + 45 * kUsPerMs), QString("01:02:03.045"));
    }

    void clipTimeMapping()
    {
        Clip c;
        c.start = 10 * kUsPerSec;
        c.in = 2 * kUsPerSec;
        c.out = 6 * kUsPerSec;
        c.speed = 2.0;
        QCOMPARE(c.duration(), TimeUs(2 * kUsPerSec));
        QCOMPARE(c.sourceToTimeline(4 * kUsPerSec), TimeUs(11 * kUsPerSec));
        QCOMPARE(c.timelineToSource(11 * kUsPerSec), TimeUs(4 * kUsPerSec));
    }

    void emptyProjectHasDefaultTracks()
    {
        Project p = makeEmptyProject("X", CanvasSettings());
        QCOMPARE(p.trackIndicesOfKind(TrackKind::Video).size(), 2);
        QCOMPARE(p.trackIndicesOfKind(TrackKind::Audio).size(), 2);
        QCOMPARE(p.canvas.aspectLabel(), QString("16:9"));
        CanvasSettings v;
        v.width = 1080;
        v.height = 1920;
        QCOMPARE(v.aspectLabel(), QString("9:16"));
    }
};

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
