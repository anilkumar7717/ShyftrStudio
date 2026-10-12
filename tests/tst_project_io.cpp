#include "TestUtil.h"

#include "core/ProjectSerializer.h"
#include "core/ProjectStore.h"

#include <QJsonArray>
#include <QJsonDocument>

using namespace ss;
using namespace ss::test;

class TestProjectIO : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

    Project sample()
    {
        Project p = makeEmptyProject("Product Launch", CanvasSettings());
        p.media << fakeVideo("m1", 8 * kUsPerSec);
        TimelineOps::addMediaClip(p, "m1", {}, 0);
        Transcript t;
        t.id = "tr1";
        t.mediaId = "m1";
        t.language = "hinglish";
        t.words = {W("Haan", 0, 300), W("bhai", 320, 600), W("chalo", 900, 1200)};
        t.cues = {{0, 1, 0, {}, false}, {2, 2, 1, QJsonObject{{"textColor", "#ffff0000"}}, true}};
        p.transcripts << t;
        Track ct;
        ct.id = "cap1";
        ct.kind = TrackKind::Caption;
        ct.name = "Captions";
        ct.transcriptId = "tr1";
        ct.styleId = "karaoke-fill-gold";
        p.tracks << ct;
        return p;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        AppPaths::setRoot(m_tmp.path() + "/root");
        QVERIFY(AppPaths::ensureLayout());
    }

    void roundTripPreservesEverything()
    {
        const Project p = sample();
        Project back;
        QString err;
        QVERIFY2(ProjectSerializer::fromBytes(ProjectSerializer::toBytes(p), back, &err), qPrintable(err));
        QCOMPARE(ProjectSerializer::toBytes(back), ProjectSerializer::toBytes(p));
        QCOMPARE(back.transcripts[0].words[1].text, QString("bhai"));
        QCOMPARE(back.transcripts[0].cues[1].styleOverrides.value("textColor").toString(), QString("#ffff0000"));
        QVERIFY(back.transcripts[0].cues[1].manual);
    }

    void validationCatchesProblems()
    {
        Project p = sample();
        p.tracks[0].clips << p.tracks[0].clips[0]; // duplicate id + overlap
        const QStringList problems = ProjectSerializer::validate(p);
        QVERIFY(problems.size() >= 2);
        Project q = sample();
        q.transcripts[0].cues[1].first = 0; // overlapping cue
        QVERIFY(!ProjectSerializer::validate(q).isEmpty());
    }

    void newerSchemaIsRejectedWithoutTouchingFile()
    {
        QJsonObject root = ProjectSerializer::toJson(sample());
        root["schemaVersion"] = 99;
        Project out;
        QString err;
        QVERIFY(!ProjectSerializer::fromJson(root, out, &err));
        QVERIFY(err.contains("newer version"));
    }

    void migratesV1()
    {
        // A v1 prototype project: millisecond times and root-level captions.
        QJsonObject clip{{"id", "c1"}, {"type", "media"}, {"mediaId", "m1"}, {"name", "a"},
                         {"startMs", 1000}, {"inMs", 0}, {"outMs", 5000}};
        QJsonObject track{{"id", "t1"}, {"kind", "video"}, {"name", "V1"}, {"clips", QJsonArray{clip}}};
        QJsonObject media{{"id", "m1"}, {"name", "a.mp4"}, {"path", "/x/a.mp4"}, {"kind", "video"}, {"durationMs", 5000},
                          {"width", 1280}, {"height", 720}, {"hasAudio", true}};
        QJsonObject caps{{"mediaId", "m1"}, {"language", "en"}, {"template", "minimal-clean"},
                         {"words", QJsonArray{QJsonObject{{"t", "hello"}, {"s", 100}, {"e", 400}}}}};
        QJsonObject root{{"schemaVersion", 1}, {"id", "p"}, {"name", "Old"},
                         {"canvas", QJsonObject{{"width", 1280}, {"height", 720}, {"fps", "25/1"}}},
                         {"media", QJsonArray{media}}, {"tracks", QJsonArray{track}}, {"captions", caps},
                         {"playheadMs", 2500}};
        Project p;
        QString err;
        int from = 0;
        QVERIFY2(ProjectSerializer::fromJson(root, p, &err, &from), qPrintable(err));
        QCOMPARE(from, 1);
        QCOMPARE(p.tracks[0].clips[0].start, TimeUs(1 * kUsPerSec));
        QCOMPARE(p.media[0].duration, TimeUs(5 * kUsPerSec));
        QCOMPARE(p.transcripts.size(), 1);
        QCOMPARE(p.transcripts[0].words[0].end, TimeUs(400 * kUsPerMs));
        QVERIFY(p.tracks.last().kind == TrackKind::Caption);
        QCOMPARE(p.tracks.last().styleId, QString("minimal-clean"));
        QCOMPARE(p.playhead, TimeUs(2500 * kUsPerMs));
    }

    void atomicSaveRotatesBackupsAndRecoversFromCorruption()
    {
        const QString dir = m_tmp.path() + "/proj1";
        Project p = sample();
        QString err;
        QVERIFY2(ProjectStore::createProject(dir, p, &err), qPrintable(err));
        for (const QString &sub : ProjectStore::subfolders())
            QVERIFY(QDir(dir + "/" + sub).exists());
        for (int i = 0; i < 7; ++i) {
            p.name = QString("Rev %1").arg(i);
            QVERIFY(ProjectStore::save(p, dir, &err, 3));
            QTest::qWait(5);
        }
        QCOMPARE(ProjectStore::backups(dir).size(), 3);
        QVERIFY(QFileInfo::exists(dir + "/subtitles/transcript-tr1.json"));

        // Simulate a torn write by a crash: project.json is garbage.
        QFile f(dir + "/project.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"schemaVersion\": 2, \"tracks\": [");
        f.close();
        Project loaded;
        LoadReport rep;
        QVERIFY2(ProjectStore::load(dir, loaded, &rep, &err), qPrintable(err));
        QVERIFY(!rep.warning.isEmpty());
        QVERIFY(loaded.name.startsWith("Rev"));
    }

    void refusesToSaveInvalidProject()
    {
        const QString dir = m_tmp.path() + "/proj2";
        Project p = sample();
        QString err;
        QVERIFY(ProjectStore::createProject(dir, p, &err));
        const QByteArray before = [&] { QFile f(dir + "/project.json"); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        p.tracks[0].clips[0].speed = -1;
        QVERIFY(!ProjectStore::save(p, dir, &err));
        QFile f(dir + "/project.json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before); // untouched
    }

    void autosaveRollingAndRecoveryInfo()
    {
        const QString dir = m_tmp.path() + "/proj3";
        Project p = sample();
        QString err;
        QVERIFY(ProjectStore::createProject(dir, p, &err));
        ProjectStore::markSessionOpen(dir);
        for (int i = 0; i < 6; ++i) {
            QVERIFY(!ProjectStore::writeAutosave(p, dir, 4).isEmpty());
            QTest::qWait(5);
        }
        QCOMPARE(ProjectStore::autosaves(dir).size(), 4);
        RecoveryInfo r = ProjectStore::recoveryInfo(dir);
        QVERIFY(r.uncleanShutdown); // never marked closed => crash
        QVERIFY(!r.autosavePath.isEmpty());
        Project rec;
        QVERIFY(ProjectStore::loadFile(r.autosavePath, rec, nullptr, &err));
        QCOMPARE(rec.name, p.name);
        ProjectStore::markSessionClosed(dir);
        QVERIFY(!ProjectStore::recoveryInfo(dir).uncleanShutdown);
    }

    void relinkFindsMovedMedia()
    {
        const QString dir = m_tmp.path() + "/proj4";
        const QString mediaDir = m_tmp.path() + "/moved/deeper";
        QDir().mkpath(mediaDir);
        QFile mf(mediaDir + "/m1.mp4");
        QVERIFY(mf.open(QIODevice::WriteOnly));
        mf.write(QByteArray(1234, 'x'));
        mf.close();
        Project p = sample();
        p.media[0].fileSize = 1234;
        QString err;
        QVERIFY(ProjectStore::createProject(dir, p, &err));
        Project loaded;
        LoadReport rep;
        QVERIFY(ProjectStore::load(dir, loaded, &rep, &err));
        QCOMPARE(rep.missingMediaIds, QStringList{"m1"});
        const auto found = ProjectStore::searchForMissing(loaded, m_tmp.path() + "/moved");
        QCOMPARE(found.value("m1"), QFileInfo(mediaDir + "/m1.mp4").absoluteFilePath());
        QCOMPARE(ProjectStore::applyRelink(loaded, found, dir), 1);
        QVERIFY(!loaded.media[0].missing);
        // collect media into the project folder (portable)
        QVERIFY(ProjectStore::estimateCollectBytes(loaded, dir) == 1234);
        QVERIFY2(ProjectStore::collectMedia(loaded, dir, &err), qPrintable(err));
        QVERIFY(loaded.media[0].path.startsWith(QDir(dir).absolutePath() + "/media/"));
        QCOMPARE(loaded.media[0].relativePath, QString("media/m1.mp4"));
        // package to another location and reopen it
        QVERIFY2(ProjectStore::packageProject(loaded, dir, m_tmp.path() + "/pkg", &err), qPrintable(err));
        Project pkg;
        LoadReport rep2;
        QVERIFY(ProjectStore::load(m_tmp.path() + "/pkg/Product Launch.shyftr", pkg, &rep2, &err));
        QVERIFY(rep2.missingMediaIds.isEmpty());
    }

    void settingsAndShortcutsPersist()
    {
        Settings s;
        s.autosaveIntervalSec = 45;
        s.highContrast = true;
        Settings::instance() = s;
        QVERIFY(Settings::instance().save());
        Settings::instance() = Settings();
        QVERIFY(Settings::instance().load());
        QCOMPARE(Settings::instance().autosaveIntervalSec, 45);
        QVERIFY(Settings::instance().highContrast);

        auto &sc = Shortcuts::instance();
        sc.registerAction("split", "Split", QKeySequence("S"));
        sc.registerAction("ripple", "Ripple delete", QKeySequence("Shift+Del"));
        sc.setKey("ripple", QKeySequence("S"));
        QCOMPARE(sc.conflicts().size(), 1);
        sc.setKey("ripple", QKeySequence("Shift+Backspace"));
        QVERIFY(sc.save());
        sc.setKey("ripple", QKeySequence("X"));
        QVERIFY(sc.load());
        QCOMPARE(sc.key("ripple"), QKeySequence("Shift+Backspace"));
    }
};

QTEST_GUILESS_MAIN(TestProjectIO)
#include "tst_project_io.moc"
