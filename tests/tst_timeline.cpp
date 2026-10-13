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

    // ---------------------------------------------------------------- drag and drop placement
    void dropOnFreeSpotLandsExactly()
    {
        Project p = base();
        const QString vt = v1(p).id;
        const auto nv = p.trackIndicesOfKind(TrackKind::Video).size();
        const QStringList ids = placeMedia(p, {"m2"}, vt, 7 * kUsPerSec);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(p.findClip(ids[0])->start, 7 * kUsPerSec);
        QCOMPARE(p.trackIndicesOfKind(TrackKind::Video).size(), nv);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void dropOnBusySpotMakesNewTrackAbove()
    {
        Project p = base();
        const QString first = addMediaClip(p, "m1", {}, 0); // V1 0..10 s
        const QString vt = v1(p).id;
        const QStringList ids = placeMedia(p, {"m2"}, vt, 3 * kUsPerSec);
        QCOMPARE(ids.size(), 1);
        const Track *t = nullptr;
        const Clip *c = p.findClip(ids[0], &t);
        QCOMPARE(c->start, 3 * kUsPerSec);          // not pushed to 10 s
        QVERIFY(t->id != vt);                        // on a new track ...
        QVERIFY(p.trackIndex(t->id) == p.trackIndex(vt) + 1); // ... directly above V1
        QCOMPARE(p.findClip(first)->start, TimeUs(0));
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void dropWithInsertPushesClipsAndLinkedAudio()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", {}, 0);              // V1 0..4
        const QString b = addMediaClip(p, "m1", v1(p).id, 4 * kUsPerSec); // V1 4..14 (+ linked audio)
        // drop at 5 s (inside b, nearer its start) -> inserts at 4 s
        const auto nv = p.trackIndicesOfKind(TrackKind::Video).size();
        const QStringList ids = placeMedia(p, {"m2"}, v1(p).id, 5 * kUsPerSec, PlaceMode::Insert);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(p.findClip(ids[0])->start, 4 * kUsPerSec);
        QCOMPARE(p.findClip(a)->start, TimeUs(0));
        QCOMPARE(p.findClip(b)->start, 8 * kUsPerSec);
        for (Clip *l : p.linkedClips(b))
            QCOMPARE(l->start, 8 * kUsPerSec); // its audio moved with it
        QCOMPARE(p.trackIndicesOfKind(TrackKind::Video).size(), nv);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void dropOnNewTrackLane()
    {
        Project p = base();
        addMediaClip(p, "m2", {}, 0);
        const int before = p.trackIndicesOfKind(TrackKind::Video).size();
        const QStringList ids = placeMedia(p, {"m2", "m2"}, kNewTrack, 2 * kUsPerSec);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(p.trackIndicesOfKind(TrackKind::Video).size(), before + 1);
        QCOMPARE(p.findClip(ids[1])->start, 6 * kUsPerSec); // second follows the first
        const Track *t0 = nullptr, *t1 = nullptr;
        p.findClip(ids[0], &t0);
        p.findClip(ids[1], &t1);
        QCOMPARE(t0->id, t1->id);
    }

    void moveOntoBusyTrackGoesToNewTrack()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", {}, 0, false);           // V1 0..4
        const QString v2 = addTrack(p, TrackKind::Video);
        const QString b = addMediaClip(p, "m2", v2, 0, false);           // V2 0..4
        // drag b down onto V1 at +1 s: V1 is busy there
        QVERIFY(moveClipsPlaced(p, {b}, b, kUsPerSec, v1(p).id, PlaceMode::Auto));
        const Track *t = nullptr;
        const Clip *c = p.findClip(b, &t);
        QCOMPARE(c->start, kUsPerSec);
        QVERIFY(t->id != v1(p).id && t->id != v2);
        QCOMPARE(p.findClip(a)->start, TimeUs(0));
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void moveWithInsertReordersClips()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", {}, 0, false);                // 0..4
        const QString b = addMediaClip(p, "m2", v1(p).id, 4 * kUsPerSec, false); // 4..8
        const QString c = addMediaClip(p, "m2", v1(p).id, 8 * kUsPerSec, false); // 8..12
        // drag c to 0.5 s with insert: it goes first and the others move later
        QVERIFY(moveClipsPlaced(p, {c}, c, -7500 * kUsPerMs, {}, PlaceMode::Insert));
        QCOMPARE(p.findClip(c)->start, TimeUs(0));
        QCOMPARE(p.findClip(a)->start, 4 * kUsPerSec);
        QCOMPARE(p.findClip(b)->start, 8 * kUsPerSec);
        QVERIFY(ProjectSerializer::validate(p).isEmpty());
    }

    void moveIntoFreeSpaceIsExact()
    {
        Project p = base();
        const QString a = addMediaClip(p, "m2", {}, 0, false);
        QVERIFY(moveClipsPlaced(p, {a}, a, 20 * kUsPerSec, {}, PlaceMode::Auto));
        QCOMPARE(p.findClip(a)->start, 20 * kUsPerSec);
        QVERIFY(moveClipsPlaced(p, {a}, a, 0, kNewTrack, PlaceMode::Auto));
        const Track *t = nullptr;
        p.findClip(a, &t);
        QVERIFY(t->id != v1(p).id);
    }

    void graphicOnBusyTrackGoesAbove()
    {
        Project p = base();
        addMediaClip(p, "m1", {}, 0);
        TitleProps tp;
        tp.text = "Hi";
        const QString id = addTitleClip(p, v1(p).id, 2 * kUsPerSec, 3 * kUsPerSec, tp);
        QCOMPARE(p.findClip(id)->start, 2 * kUsPerSec);
        const Track *t = nullptr;
        p.findClip(id, &t);
        QVERIFY(t->id != v1(p).id);
    }

    void reorderTracks()
    {
        Project p = base();
        const QString v2 = addTrack(p, TrackKind::Video);
        const QString v1id = v1(p).id;
        QVERIFY(moveTrackTo(p, v2, p.trackIndex(v1id)));
        QCOMPARE(v1(p).id, v2);
        QVERIFY(!moveTrackTo(p, v2, p.trackIndicesOfKind(TrackKind::Audio)[0])); // never across kinds
    }
};

QTEST_GUILESS_MAIN(TestTimeline)
#include "tst_timeline.moc"
