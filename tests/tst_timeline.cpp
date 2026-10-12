#include "TestUtil.h"

#include "core/Document.h"
#include "core/ProjectSerializer.h"

using namespace ss;
using namespace ss::test;
using namespace ss::TimelineOps;

class TestTimeline : public QObject {
    Q_OBJECT

    Project base()
    {
        Project p = makeEmptyProject("T", CanvasSettings());
        p.media << fakeVideo("m1", 10 * kUsPerSec) << fakeVideo("m2", 4 * kUsPerSec, false);
        return p;
    }
    const Track &v1(const Project &p) { return p.tracks[p.trackIndicesOfKind(TrackKind::Video)[0]]; }
    const Track &a1(const Project &p) { return p.tracks[p.trackIndicesOfKind(TrackKind::Audio)[0]]; }

private slots:
    void addMediaCreatesLinkedAudio()
    {
        Project p = base();
        const QString id = addMediaClip(p, "m1", {}, 0);
        QVERIFY(!id.isEmpty());
        QCOMPARE(v1(p).clips.size(), 1);
        QCOMPARE(a1(p).clips.size(), 1);
        QCOMPARE(v1(p).clips[0].linkId, a1(p).clips[0].linkId);
        QCOMPARE(p.linkedClips(id).size(), 2);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void addSecondClipDoesNotOverlap()
    {
        Project p = base();
        addMediaClip(p, "m1", v1(p).id, 0);
        addMediaClip(p, "m2", v1(p).id, 2 * kUsPerSec); // requested inside first clip -> pushed after
        QCOMPARE(v1(p).clips.size(), 2);
        QCOMPARE(v1(p).clips[1].start, TimeUs(10 * kUsPerSec));
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void splitKeepsSourceContinuity()
    {
        Project p = base();
        const QString id = addMediaClip(p, "m1", {}, 0);
        QVERIFY(splitClip(p, id, 4 * kUsPerSec));
        QCOMPARE(v1(p).clips.size(), 2);
        QCOMPARE(a1(p).clips.size(), 2);
        const Clip &l = v1(p).clips[0], &r = v1(p).clips[1];
        QCOMPARE(l.out, TimeUs(4 * kUsPerSec));
        QCOMPARE(r.in, TimeUs(4 * kUsPerSec));
        QCOMPARE(r.start, TimeUs(4 * kUsPerSec));
        // right halves are linked to each other, not to the left halves
        QVERIFY(!r.linkId.isEmpty());
        QCOMPARE(r.linkId, a1(p).clips[1].linkId);
        QVERIFY(r.linkId != l.linkId);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void trimIsClampedToSourceAndNeighbours()
    {
        Project p = base();
        const QString id = addMediaClip(p, "m1", {}, 0);
        // cannot extend beyond source length
        QCOMPARE(trimClip(p, id, Edge::End, 20 * kUsPerSec), TimeUs(10 * kUsPerSec));
        // trim end to 6s
        QCOMPARE(trimClip(p, id, Edge::End, 6 * kUsPerSec), TimeUs(6 * kUsPerSec));
        QCOMPARE(a1(p).clips[0].end(), TimeUs(6 * kUsPerSec)); // linked audio follows
        // trim start to 1s
        QCOMPARE(trimClip(p, id, Edge::Start, 1 * kUsPerSec), TimeUs(1 * kUsPerSec));
        QCOMPARE(v1(p).clips[0].in, TimeUs(1 * kUsPerSec));
        // start cannot go before source in = 0
        QCOMPARE(trimClip(p, id, Edge::Start, -5 * kUsPerSec), TimeUs(0));
    }

    void moveClampsAgainstObstacles()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", v1(p).id, 0);              // 0..4
        const QString b = addMediaClip(p, "m2", v1(p).id, 6 * kUsPerSec);  // 6..10
        TimeUs applied = 0;
        QVERIFY(moveClips(p, {a}, 3 * kUsPerSec, {}, {}, true, &applied)); // would overlap b -> butt against it
        QCOMPARE(applied, TimeUs(2 * kUsPerSec));
        QCOMPARE(p.findClip(a)->end(), p.findClip(b)->start);
        QVERIFY(!moveClips(p, {b}, -20 * kUsPerSec, {}, {}, true, &applied) || p.findClip(b)->start >= 0);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void moveToAnotherTrack()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", v1(p).id, 0);
        const QString v2 = p.tracks[p.trackIndicesOfKind(TrackKind::Video)[1]].id;
        QVERIFY(moveClips(p, {a}, 0, a, v2));
        const Track *t = nullptr;
        p.findClip(a, &t);
        QCOMPARE(t->id, v2);
    }

    void rippleDeleteClosesGap()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", v1(p).id, 0);
        const QString b = addMediaClip(p, "m2", v1(p).id, 4 * kUsPerSec);
        const QString c = addMediaClip(p, "m2", v1(p).id, 8 * kUsPerSec);
        QVERIFY(deleteClips(p, {b}, true));
        QCOMPARE(v1(p).clips.size(), 2);
        QCOMPARE(p.findClip(c)->start, TimeUs(4 * kUsPerSec));
        QVERIFY(deleteClips(p, {a}, false));
        QCOMPARE(p.findClip(c)->start, TimeUs(4 * kUsPerSec)); // non-ripple leaves gap
    }

    void speedChangeRespectsNextClip()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", v1(p).id, 0);
        addMediaClip(p, "m2", v1(p).id, 4 * kUsPerSec);
        QVERIFY(!setClipSpeed(p, a, 0.5)); // would double length into next clip
        QVERIFY(setClipSpeed(p, a, 2.0));
        QCOMPARE(p.findClip(a)->duration(), TimeUs(2 * kUsPerSec));
    }

    void snapFindsNearestEdge()
    {
        Project p = base();
        addMediaClip(p, "m2", v1(p).id, 0);
        bool snapped = false;
        const auto pts = snapPoints(p);
        QCOMPARE(snap(pts, 4 * kUsPerSec + 30 * kUsPerMs, 50 * kUsPerMs, &snapped), TimeUs(4 * kUsPerSec));
        QVERIFY(snapped);
    }

    void documentUndoRedo()
    {
        Document doc;
        Project p = base();
        doc.reset(p, "/tmp/x");
        QString clipId;
        QVERIFY(doc.apply("Add", [&](Project &pr) {
            clipId = addMediaClip(pr, "m1", {}, 0);
            return !clipId.isEmpty();
        }));
        QVERIFY(doc.project().findClip(clipId));
        QVERIFY(doc.apply("Split", [&](Project &pr) { return splitClip(pr, clipId, 5 * kUsPerSec); }));
        QCOMPARE(doc.project().tracks[0].clips.size(), 2);
        doc.undoStack()->undo();
        QCOMPARE(doc.project().tracks[0].clips.size(), 1);
        doc.undoStack()->undo();
        QVERIFY(!doc.project().findClip(clipId));
        doc.undoStack()->redo();
        doc.undoStack()->redo();
        QCOMPARE(doc.project().tracks[0].clips.size(), 2);
        // invalid edits are rejected and leave no undo entry
        const int count = doc.undoStack()->count();
        QVERIFY(!doc.apply("Break", [](Project &pr) {
            pr.tracks[0].clips[0].out = pr.tracks[0].clips[0].in; // empty range
            return true;
        }));
        QCOMPARE(doc.undoStack()->count(), count);
    }

    void documentMergesSliderEdits()
    {
        Document doc;
        Project p = base();
        QString id = addMediaClip(p, "m2", {}, 0);
        doc.reset(p, "/tmp/x");
        for (int i = 1; i <= 10; ++i)
            doc.apply("Gain", [&](Project &pr) {
                pr.findClip(id)->audio.gainDb = i;
                return true;
            }, "gain:" + id);
        QCOMPARE(doc.undoStack()->count(), 1);
        doc.undoStack()->undo();
        QCOMPARE(doc.project().findClip(id)->audio.gainDb, 0.0);
    }
};

QTEST_GUILESS_MAIN(TestTimeline)
#include "tst_timeline.moc"
