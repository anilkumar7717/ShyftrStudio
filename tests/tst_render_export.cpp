#include "TestUtil.h"

#include "captions/CaptionEngine.h"
#include "captions/OverlayRenderer.h"
#include "media/CanvasLayout.h"
#include "media/ColorMath.h"
#include "media/Compositor.h"
#include "media/FrameReader.h"
#include "media/ExportGraph.h"
#include "media/ExportJob.h"
#include "media/MediaCache.h"
#include "media/MediaProbe.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>

using namespace ss;
using namespace ss::test;

static double meanAbsDiff(const QImage &a0, const QImage &b0, const QRect &r = QRect())
{
    QImage a = a0.convertToFormat(QImage::Format_RGB32);
    QImage b = b0.convertToFormat(QImage::Format_RGB32);
    const QRect rr = r.isNull() ? a.rect() : r;
    double sum = 0;
    qint64 n = 0;
    for (int y = rr.top(); y <= rr.bottom(); ++y)
        for (int x = rr.left(); x <= rr.right(); ++x) {
            const QRgb pa = a.pixel(x, y), pb = b.pixel(x, y);
            sum += std::abs(qRed(pa) - qRed(pb)) + std::abs(qGreen(pa) - qGreen(pb)) + std::abs(qBlue(pa) - qBlue(pb));
            n += 3;
        }
    return n ? sum / n : 0;
}

static QImage extractFrame(const QString &video, double atSec, const QString &outPng)
{
    Tools::run(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-ss", QString::number(atSec, 'f', 3), "-i",
                                 video, "-frames:v", "1", outPng});
    return QImage(outPng);
}

static bool runExport(ExportRequest req, QString *err)
{
    ExportJob job(std::move(req));
    QSignalSpy ok(&job, &ExportJob::succeeded);
    QSignalSpy bad(&job, &ExportJob::failedWithReport);
    job.start();
    while (!job.isFinished())
        QTest::qWait(50);
    QCoreApplication::processEvents();
    if (!bad.isEmpty() && err) {
        *err = bad.first().at(0).toString() + "\nlog: " + bad.first().at(1).toString();
        QFile f(bad.first().at(1).toString());
        if (f.open(QIODevice::ReadOnly))
            *err += "\n" + QString::fromUtf8(f.readAll()).right(3000);
    }
    return ok.size() == 1;
}

static QJsonObject probeJson(const QString &file)
{
    const auto r = Tools::run(Tools::ffprobe(), {"-v", "error", "-count_frames", "-print_format", "json", "-show_streams",
                                                 "-show_format", file});
    return QJsonDocument::fromJson(r.out).object();
}

class TestRenderExport : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

    CueInstance cue(TimeUs start)
    {
        CueInstance c;
        c.start = start;
        c.end = start + 2 * kUsPerSec;
        const QStringList w{"Captions", "rendered", "locally"};
        for (int i = 0; i < w.size(); ++i)
            c.words << WordInstance{i, w[i], start + i * 600 * kUsPerMs, start + (i + 1) * 600 * kUsPerMs};
        return c;
    }

private slots:
    void initTestCase()
    {
        AppPaths::setRoot(m_tmp.path() + "/root");
        AppPaths::ensureLayout();
    }

    void everyTemplateDrawsInsideTheFrame()
    {
        const QSize size(1080, 1920);
        for (const CaptionStyle &s : TemplateLibrary::instance().builtIns()) {
            QImage img(size, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            CaptionRenderer::SpeakerInfo spk{"Anil", QColor("#FFD43B")};
            CueInstance c = cue(0);
            c.speaker = 0;
            CaptionRenderer::paintCue(p, size, s, c, 900 * kUsPerMs, &spk);
            p.end();
            int painted = 0;
            QRect bounds;
            for (int y = 0; y < img.height(); y += 2)
                for (int x = 0; x < img.width(); x += 2)
                    if (qAlpha(img.pixel(x, y)) > 0) {
                        ++painted;
                        bounds |= QRect(x, y, 1, 1);
                    }
            QVERIFY2(painted > 50, qPrintable(s.id));
            if (s.boxMode != "bar")
                QVERIFY2(bounds.left() > 0 && bounds.right() < size.width() - 1, qPrintable(s.id + " escapes horizontally"));
            QVERIFY2(bounds.bottom() < size.height() - 1, qPrintable(s.id));
        }
    }

    void highlightColoursActiveWord()
    {
        CaptionStyle s = TemplateLibrary::instance().get("bold-highlight-yellow");
        s.shadowColor = Qt::transparent;
        s.entry = "none";
        const QSize size(1280, 720);
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        CaptionRenderer::paintCue(p, size, s, cue(0), 700 * kUsPerMs); // second word active
        p.end();
        int yellow = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c(img.pixel(x, y));
                if (c.red() > 230 && c.green() > 190 && c.blue() < 100)
                    ++yellow;
            }
        QVERIFY(yellow > 100);
    }

    void colorMathMatchesFfmpeg()
    {
        if (Tools::ffmpeg().isEmpty())
            QSKIP("FFmpeg not available");
        const QString src = m_tmp.path() + "/color-src.png";
        Tools::run(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x240",
                                     "-frames:v", "1", src});
        ColorAdjust c;
        c.exposure = 0.4;
        c.contrast = 1.25;
        c.saturation = 1.4;
        c.temperature = 0.5;
        c.tint = -0.3;
        const QString out = m_tmp.path() + "/color-ff.png";
        const auto r = Tools::run(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-i", src, "-vf",
                                                    "format=rgba," + ColorMath::ffmpegFilter(c), "-frames:v", "1", out});
        QCOMPARE(r.exitCode, 0);
        QImage mine(src);
        mine = mine.convertToFormat(QImage::Format_ARGB32);
        ColorMath::apply(mine, c);
        const double diff = meanAbsDiff(mine, QImage(out));
        QVERIFY2(diff < 2.0, qPrintable(QString("preview/export colour diff %1").arg(diff)));
    }

    void geometryFitFillStretch()
    {
        Transform t;
        ClipGeometry g = computeGeometry(1920, 1080, t, 1080, 1920); // 16:9 into 9:16, fit
        QCOMPARE(int(g.outW), 1080);
        QCOMPARE(int(g.outH), 608);
        t.fit = "fill";
        g = computeGeometry(1920, 1080, t, 1080, 1920);
        QCOMPARE(int(g.outH), 1920);
        QVERIFY(g.x < 0);
        t.rotation = 90;
        t.fit = "fit";
        g = computeGeometry(1920, 1080, t, 1080, 1920);
        QCOMPARE(g.quarterTurns, 1);
        QCOMPARE(int(g.outW), 1080);
        QCOMPARE(int(g.outH), 1920);
        t.rotation = 0;
        t.cropL = 0.25;
        t.cropR = 0.25;
        g = computeGeometry(1920, 1080, t, 1920, 1080);
        QCOMPARE(int(g.crop.width()), 960);
    }

    void probeAndPeaks()
    {
        const QString v = makeTestVideo(m_tmp.path(), "probe.mp4", 2.0, 320, 240, "30000/1001");
        if (v.isEmpty())
            QSKIP("FFmpeg not available");
        MediaAsset m;
        QString err;
        QVERIFY2(MediaProbe::probe(v, m, &err), qPrintable(err));
        QVERIFY(m.kind == MediaKind::Video);
        QCOMPARE(m.width, 320);
        QCOMPARE(m.fps.num, 30000);
        QVERIFY(m.hasAudio);
        QVERIFY(std::abs(m.duration - 2 * kUsPerSec) < 100 * kUsPerMs);
        QByteArray pcm(8000 * 2, '\0');
        const auto pk = MediaCache::computePeaks(pcm, 8000, 100);
        QCOMPARE(pk.size(), 100);
        QVERIFY(MediaCache::savePeaks(m_tmp.path() + "/x.peaks", pk));
        QCOMPARE(MediaCache::loadPeaks(m_tmp.path() + "/x.peaks").size(), 100);
    }

    void overlayExportMatchesPreview()
    {
        if (Tools::ffmpeg().isEmpty())
            QSKIP("FFmpeg not available");
        Project p = makeEmptyProject("Parity", CanvasSettings{640, 360, {25, 1}, 48000, Qt::black});
        TitleProps tp;
        tp.text = "PARITY";
        tp.sizePct = 0.18;
        tp.color = QColor("#ff3366");
        tp.animation = "none";
        tp.y = 0.3;
        TimelineOps::addTitleClip(p, p.tracks[0].id, 0, 2 * kUsPerSec, tp);
        Transcript t;
        t.id = "tr";
        t.words = {W("Local", 200, 700), W("captions", 750, 1300), W("only", 1350, 1900)};
        t.cues = {{0, 2, -1, {}, true}};
        p.transcripts << t;
        Track ct;
        ct.id = "ct";
        ct.kind = TrackKind::Caption;
        ct.transcriptId = "tr";
        ct.styleId = "bold-highlight-yellow";
        p.tracks << ct;

        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/parity";
        req.settings.width = 640;
        req.settings.height = 360;
        req.settings.fps = {25, 1};
        req.settings.encoder = "software";
        req.settings.crf = 4;
        req.settings.speedPreset = "ultrafast";
        req.settings.outputPath = m_tmp.path() + "/parity.mp4";
        req.overlay = OverlayScene::build(p);
        QVERIFY(!req.overlay.isEmpty());
        const OverlayScene scene = req.overlay;
        QString err;
        QVERIFY2(runExport(req, &err), qPrintable(err));
        QVERIFY(!QFileInfo::exists(m_tmp.path() + "/parity.partial.mp4"));

        const QJsonObject info = probeJson(m_tmp.path() + "/parity.mp4");
        const QJsonArray streams = info.value("streams").toArray();
        QCOMPARE(streams.size(), 2);
        QCOMPARE(streams[0].toObject().value("nb_read_frames").toString().toInt(), 50);

        for (double at : {0.5, 1.0, 1.5}) {
            const QImage exported = extractFrame(m_tmp.path() + "/parity.mp4", at, m_tmp.path() + "/f.png");
            QVERIFY(!exported.isNull());
            QImage expected(640, 360, QImage::Format_ARGB32_Premultiplied);
            expected.fill(Qt::black);
            QPainter painter(&expected);
            // ffmpeg -ss lands on the frame at/after the timestamp
            scene.render(painter, QSize(640, 360), TimeUs(std::llround(at * kUsPerSec)));
            painter.end();
            const double diff = meanAbsDiff(expected, exported);
            QVERIFY2(diff < 4.0, qPrintable(QString("frame %1s differs by %2").arg(at).arg(diff)));
        }
    }

    void canvasDragMovesAndResizes()
    {
        Project p = makeEmptyProject("Drag", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 10 * kUsPerSec);
        const QString vid = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0);
        QVERIFY(!vid.isEmpty());
        TitleProps tp;
        tp.text = "Hello";
        tp.animation = "none";
        const QString title = TimelineOps::addTitleClip(p, {}, 0, 4 * kUsPerSec, tp);
        ShapeProps sp;
        sp.shape = "arrow";
        sp.x = 0.2;
        sp.y = 0.2;
        const QString shape = TimelineOps::addShapeClip(p, {}, 0, 4 * kUsPerSec, sp);
        Transcript t;
        t.id = "tr";
        t.words = {W("Drag", 0, 500), W("me", 500, 1000), W("around", 1000, 2000)};
        t.cues = {{0, 2, -1, {}, true}};
        p.transcripts << t;
        Track ct;
        ct.id = "ct";
        ct.kind = TrackKind::Caption;
        ct.transcriptId = "tr";
        ct.styleId = "minimal-clean";
        p.tracks << ct;

        const TimeUs at = 600 * kUsPerMs;
        auto items = CanvasLayout::itemsAt(p, OverlayScene::build(p), at);
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[0].kind, CanvasLayout::Item::Caption); // top-most first
        QCOMPARE(items.last().kind, CanvasLayout::Item::Media);
        QCOMPARE(items.last().rect, QRectF(0, 0, 1920, 1080));

        // move all captions 100 px right and 54 px up
        const QRectF before = items[0].rect;
        auto g = CanvasLayout::grab(p, items[0], false);
        QVERIFY(CanvasLayout::move(p, g, QPointF(100, -54)));
        QCOMPARE(p.findTrack("ct")->styleOverrides.value("offsetYPct").toDouble(), 0.05);
        items = CanvasLayout::itemsAt(p, OverlayScene::build(p), at);
        QVERIFY(std::abs(items[0].rect.left() - before.left() - 100) < 1.5);
        QVERIFY(std::abs(items[0].rect.top() - before.top() + 54) < 1.5);

        // Alt-drag: only this caption gets its own position
        g = CanvasLayout::grab(p, items[0], true);
        QVERIFY(CanvasLayout::move(p, g, QPointF(-200, 0)));
        QVERIFY(p.findTranscript("tr")->cues[0].styleOverrides.contains("offsetXPct"));
        items = CanvasLayout::itemsAt(p, OverlayScene::build(p), at);
        QVERIFY(items[0].cueHasOwnPosition);
        QVERIFY(CanvasLayout::resetPosition(p, items[0]));
        QVERIFY(!p.findTranscript("tr")->cues[0].styleOverrides.contains("offsetXPct"));
        items = CanvasLayout::itemsAt(p, OverlayScene::build(p), at);
        QVERIFY(!items[0].cueHasOwnPosition);

        // caption size via corner drag
        g = CanvasLayout::grab(p, items[0], false);
        QVERIFY(CanvasLayout::scale(p, g, 1.5));
        QVERIFY(std::abs(p.findTrack("ct")->styleOverrides.value("sizePct").toDouble() - g.size * 1.5) < 1e-9);

        // title, shape and video
        for (const auto &it : CanvasLayout::itemsAt(p, OverlayScene::build(p), at)) {
            g = CanvasLayout::grab(p, it, false);
            QVERIFY(CanvasLayout::move(p, g, QPointF(192, 108)));
        }
        QCOMPARE(p.findClip(title)->title.x, 0.6);
        QCOMPARE(p.findClip(title)->title.y, 0.6);
        QCOMPARE(p.findClip(shape)->shape.x, 0.3);
        QCOMPARE(p.findClip(vid)->transform.posX, 0.1);
        items = CanvasLayout::itemsAt(p, OverlayScene::build(p), at);
        const auto shapeItem = *std::find_if(items.begin(), items.end(), [](const auto &i) { return i.kind == CanvasLayout::Item::Shape; });
        g = CanvasLayout::grab(p, shapeItem, false);
        QVERIFY(CanvasLayout::setShapeRect(p, g, QRectF(0, 0, 960, 108)));
        QCOMPARE(p.findClip(shape)->shape.w, 0.5);
        QCOMPARE(p.findClip(shape)->shape.x, 0.25);
        const auto videoItem = items.last();
        g = CanvasLayout::grab(p, videoItem, false);
        QVERIFY(CanvasLayout::scale(p, g, 0.5));
        QCOMPARE(p.findClip(vid)->transform.scale, 0.5);
        QVERIFY(CanvasLayout::resetSize(p, videoItem));
        QVERIFY(CanvasLayout::resetPosition(p, videoItem));
        QCOMPARE(p.findClip(vid)->transform.posX, 0.0);
        QCOMPARE(p.findClip(vid)->transform.scale, 1.0);
    }

    void individualCaptionsMoveIndependently()
    {
        Project p = makeEmptyProject("Caps", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        Transcript t;
        t.id = "tr";
        t.words = {W("One", 0, 900), W("two", 1000, 1900), W("three", 2000, 2900), W("four", 3000, 3900)};
        t.cues = {{0, 0, -1, {}, false}, {1, 1, -1, {}, false}, {2, 2, -1, {}, false}, {3, 3, -1, {}, false}};
        p.transcripts << t;
        Track ct;
        ct.id = "ct";
        ct.kind = TrackKind::Caption;
        ct.transcriptId = "tr";
        ct.styleId = "minimal-clean";
        p.tracks << ct;
        auto capAt = [&](TimeUs ms) {
            const auto items = CanvasLayout::itemsAt(p, OverlayScene::build(p), ms * kUsPerMs);
            for (const auto &i : items)
                if (i.kind == CanvasLayout::Item::Caption)
                    return i;
            return CanvasLayout::Item();
        };
        const QRectF r0 = capAt(500).rect, r1 = capAt(1500).rect, r2 = capAt(2500).rect;

        // 1. move only caption #1: the others do not move, and it lands exactly where dragged
        auto g = CanvasLayout::grab(p, capAt(1500), true);
        QVERIFY(CanvasLayout::move(p, g, QPointF(-300, -200)));
        QVERIFY(std::abs(capAt(1500).rect.left() - (r1.left() - 300)) < 1.5);
        QVERIFY(std::abs(capAt(1500).rect.top() - (r1.top() - 200)) < 1.5);
        QCOMPARE(capAt(500).rect, r0);
        QCOMPARE(capAt(2500).rect, r2);
        QVERIFY(p.findTrack("ct")->styleOverrides.isEmpty());

        // 2. moving it again continues from where it is (no jump back)
        g = CanvasLayout::grab(p, capAt(1500), true);
        QVERIFY(CanvasLayout::move(p, g, QPointF(100, 0)));
        QVERIFY(std::abs(capAt(1500).rect.left() - (r1.left() - 200)) < 1.5);

        // 3. "all captions": everyone moves by the same amount, including the moved one
        const QRectF moved1 = capAt(1500).rect;
        g = CanvasLayout::grab(p, capAt(2500), false);
        QVERIFY(CanvasLayout::move(p, g, QPointF(0, -100)));
        QVERIFY(std::abs(capAt(500).rect.top() - (r0.top() - 100)) < 1.5);
        QVERIFY(std::abs(capAt(2500).rect.top() - (r2.top() - 100)) < 1.5);
        QVERIFY(std::abs(capAt(1500).rect.top() - (moved1.top() - 100)) < 1.5);

        // 4. resizing one caption leaves the others alone; reset size for one only
        const QRectF big0 = capAt(500).rect;
        g = CanvasLayout::grab(p, capAt(2500), true);
        QVERIFY(CanvasLayout::scale(p, g, 1.6));
        QVERIFY(capAt(2500).rect.height() > big0.height() * 1.3);
        QCOMPARE(capAt(500).rect, big0);
        QVERIFY(CanvasLayout::resetSize(p, capAt(2500), true));
        QVERIFY(std::abs(capAt(2500).rect.height() - big0.height()) < 1.5);

        // 5. re-splitting the captions (e.g. 2 words per caption) keeps the moved one's position
        TranscriptEdit::resegment(*p.findTranscript("tr"), TemplateLibrary::instance().get("minimal-clean").withOverrides(QJsonObject{{"maxWordsPerLine", 2}, {"maxLines", 1}}));
        bool kept = false;
        for (const Cue &c : p.findTranscript("tr")->cues)
            kept = kept || (c.first <= 1 && c.last >= 1 && c.styleOverrides.contains("offsetXPct"));
        QVERIFY(kept);

        // 6. "put all captions at this position" and "reset all"
        QVERIFY(CanvasLayout::applyCaptionPositionToAll(p, capAt(1500)));
        for (const Cue &c : p.findTranscript("tr")->cues)
            QVERIFY(!c.styleOverrides.contains("offsetXPct"));
        QVERIFY(std::abs(capAt(2500).rect.left() - capAt(1500).rect.left()) < 200); // same anchor now
        QVERIFY(CanvasLayout::resetPosition(p, capAt(500), false));
        QVERIFY(!p.findTrack("ct")->styleOverrides.contains("offsetXPct"));
    }

    void activeWordCaptionHitBoxMatchesDrawing()
    {
        Project p = makeEmptyProject("Hit", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        Transcript t;
        t.id = "tr";
        t.words = {W("A", 0, 500), W("muchlongerword", 500, 1000), W("end", 1000, 1500)};
        t.cues = {{0, 2, -1, {}, true}};
        p.transcripts << t;
        Track ct;
        ct.id = "ct";
        ct.kind = TrackKind::Caption;
        ct.transcriptId = "tr";
        ct.styleId = "minimal-clean";
        ct.styleOverrides = QJsonObject{{"wordEffect", "activeOnly"}};
        p.tracks << ct;
        const auto a = CanvasLayout::itemsAt(p, OverlayScene::build(p), 200 * kUsPerMs);
        const auto b = CanvasLayout::itemsAt(p, OverlayScene::build(p), 700 * kUsPerMs);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        // one short word vs one long word: the grab box follows the word that is shown
        QVERIFY(a[0].rect.width() * 3 < b[0].rect.width());
        // and it matches the drawn pixels
        QImage img(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter pa(&img);
        OverlayScene::build(p).renderCaptions(pa, img.size(), 700 * kUsPerMs);
        pa.end();
        QRect ink;
        for (int y = 0; y < img.height(); y += 2)
            for (int x = 0; x < img.width(); x += 2)
                if (qGray(img.pixel(x, y)) > 60)
                    ink |= QRect(x, y, 1, 1);
        QVERIFY(!ink.isNull());
        QVERIFY(b[0].rect.adjusted(-20, -20, 20, 20).contains(QRectF(ink)));
        QVERIFY(ink.width() > b[0].rect.width() * 0.6);
    }

    void compositorMatchesFilterGraph()
    {
        const QString v = makeTestVideo(m_tmp.path(), "cmp.mp4", 2.0, 640, 360, "25");
        if (v.isEmpty())
            QSKIP("FFmpeg not available");
        Project p = makeEmptyProject("Cmp", CanvasSettings{640, 360, {25, 1}, 48000, Qt::black});
        MediaAsset m;
        m.id = "m";
        QString err;
        QVERIFY2(MediaProbe::probe(v, m, &err), qPrintable(err));
        p.media << m;
        const QString c = TimelineOps::addMediaClip(p, "m", p.tracks[0].id, 0);
        Clip *cl = p.findClip(c);
        cl->transform.cropL = 0.1;
        cl->transform.scale = 0.8;
        cl->transform.posX = 0.05;
        cl->color.saturation = 0.5;
        cl->color.exposure = 0.2;
        TitleProps tp;
        tp.text = "SAME";
        tp.animation = "none";
        tp.sizePct = 0.15;
        TimelineOps::addTitleClip(p, p.tracks[1].id, 0, 2 * kUsPerSec, tp);
        QVERIFY(!Compositor::projectNeedsCompositor(p));

        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/cmp";
        req.settings.width = 640;
        req.settings.height = 360;
        req.settings.fps = {25, 1};
        req.settings.encoder = "software";
        req.settings.crf = 4;
        req.settings.speedPreset = "ultrafast";
        req.settings.outputPath = m_tmp.path() + "/graph.mp4";
        req.overlay = OverlayScene::build(p);
        QVERIFY2(runExport(req, &err), qPrintable(err));
        req.forceCompositor = true;
        req.settings.outputPath = m_tmp.path() + "/comp.mp4";
        QVERIFY2(runExport(req, &err), qPrintable(err));
        for (double at : {0.4, 1.2}) {
            const QImage a = extractFrame(m_tmp.path() + "/graph.mp4", at, m_tmp.path() + "/g.png");
            const QImage b = extractFrame(m_tmp.path() + "/comp.mp4", at, m_tmp.path() + "/c.png");
            QVERIFY(!a.isNull() && !b.isNull());
            const double diff = meanAbsDiff(a, b);
            QVERIFY2(diff < 8.0, qPrintable(QString("frame %1s differs by %2").arg(at).arg(diff)));
        }
    }

    void compositorExportsTransitionsAndEffects()
    {
        const QString v1 = makeTestVideo(m_tmp.path(), "t1.mp4", 2.0, 640, 360, "30");
        const QString v2 = makeTestVideo(m_tmp.path(), "t2.mp4", 2.0, 320, 240, "30");
        if (v1.isEmpty() || v2.isEmpty())
            QSKIP("FFmpeg not available");
        Project p = makeEmptyProject("Fx", CanvasSettings{640, 360, {30, 1}, 48000, Qt::black});
        QString err;
        for (const QString &f : {v1, v2}) {
            MediaAsset m;
            m.id = newId();
            QVERIFY2(MediaProbe::probe(f, m, &err), qPrintable(err));
            p.media << m;
        }
        const QString a = TimelineOps::addMediaClip(p, p.media[0].id, p.tracks[0].id, 0);
        const QString b = TimelineOps::addMediaClip(p, p.media[1].id, p.tracks[0].id, 2 * kUsPerSec);
        Clip *cb = p.findClip(b);
        cb->fx.transitionIn = {"slide-left", 600 * kUsPerMs};
        cb->fx.effects = {EffectSpec{"vignette", 0.8, 1}};
        cb->fx.background.mode = "blur";
        Clip *ca = p.findClip(a);
        ca->fx.keyframes = {Keyframe{0, 0, 0, 1, 0, 1, "linear"}, Keyframe{2 * kUsPerSec, 0, 0, 1.5, 10, 1, "linear"}};
        ca->fx.adjust.hue = 40;
        ClipFx layer;
        layer.effects = {EffectSpec{"grain", 0.3, 1}};
        TimelineOps::addEffectClip(p, p.tracks[1].id, 3 * kUsPerSec, kUsPerSec, layer, "Grain");
        QVERIFY(Compositor::projectNeedsCompositor(p));

        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/fx";
        req.settings.width = 640;
        req.settings.height = 360;
        req.settings.fps = {30, 1};
        req.settings.encoder = "software";
        req.settings.crf = 4;
        req.settings.speedPreset = "ultrafast";
        req.settings.outputPath = m_tmp.path() + "/fx.mp4";
        req.overlay = OverlayScene::build(p);
        QVERIFY2(runExport(req, &err), qPrintable(err));
        const QJsonObject info = probeJson(m_tmp.path() + "/fx.mp4");
        QCOMPARE(info.value("streams").toArray()[0].toObject().value("nb_read_frames").toString().toLongLong(),
                 usToFrameRound(p.duration(), {30, 1}));
        // the exported frames are exactly what the compositor draws (minus encoding)
        const OverlayScene scene = OverlayScene::build(p);
        for (double at : {1.0, 2.0, 3.5}) {
            const TimeUs t = TimeUs(std::llround(at * kUsPerSec));
            FrameReader reader(p, QSize(640, 360));
            QImage expected(640, 360, QImage::Format_ARGB32_Premultiplied);
            Compositor::render(expected, p, scene, t, reader);
            const QImage exported = extractFrame(m_tmp.path() + "/fx.mp4", at, m_tmp.path() + "/fx.png");
            const double diff = meanAbsDiff(expected, exported);
            QVERIFY2(diff < 6.0, qPrintable(QString("frame %1s differs by %2").arg(at).arg(diff)));
        }
        // mid-transition: half of A, half of B (not a plain cut)
        FrameReader r1(p, QSize(640, 360)), r2(p, QSize(640, 360)), r3(p, QSize(640, 360));
        QImage mid(640, 360, QImage::Format_ARGB32_Premultiplied), before(mid), after(mid);
        Compositor::render(mid, p, scene, 2 * kUsPerSec, r1);
        Compositor::render(before, p, scene, 1600 * kUsPerMs, r2);
        Compositor::render(after, p, scene, 2400 * kUsPerMs, r3);
        QVERIFY(meanAbsDiff(mid, before) > 5 && meanAbsDiff(mid, after) > 5);
    }

    void audioOnlyAndGifExports()
    {
        const QString v = makeTestVideo(m_tmp.path(), "ag.mp4", 1.5, 320, 180, "30");
        if (v.isEmpty())
            QSKIP("FFmpeg not available");
        Project p = makeEmptyProject("AG", CanvasSettings{320, 180, {30, 1}, 48000, Qt::black});
        MediaAsset m;
        m.id = "m";
        QString err;
        QVERIFY2(MediaProbe::probe(v, m, &err), qPrintable(err));
        p.media << m;
        const QString c = TimelineOps::addMediaClip(p, "m", p.tracks[0].id, 0);
        // voice effect + denoise on the linked audio clip
        for (Clip *x : p.linkedClips(c)) {
            x->fx.audio.voice = "deep";
            x->fx.audio.denoise = true;
        }
        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/ag";
        req.settings.width = 320;
        req.settings.height = 180;
        req.settings.fps = {30, 1};
        req.settings.normalizeLoudness = true;
        req.overlay = OverlayScene::build(p);
        for (const QString &ext : QStringList{"wav", "m4a"}) {
            req.settings.outputPath = m_tmp.path() + "/audio." + ext;
            QVERIFY2(runExport(req, &err), qPrintable(err));
            const QJsonArray streams = probeJson(req.settings.outputPath).value("streams").toArray();
            QCOMPARE(streams.size(), 1);
            QCOMPARE(streams[0].toObject().value("codec_type").toString(), QString("audio"));
        }
        req.settings.outputPath = m_tmp.path() + "/clip.gif";
        QVERIFY2(runExport(req, &err), qPrintable(err));
        QFile gif(req.settings.outputPath);
        QVERIFY(gif.open(QIODevice::ReadOnly));
        QVERIFY(gif.read(6).startsWith("GIF8"));
        QVERIFY(!QFileInfo::exists(m_tmp.path() + "/clip.partial.mp4"));
    }

    void fullTimelineExport()
    {
        const QString v1 = makeTestVideo(m_tmp.path(), "a.mp4", 4.0, 640, 360, "30");
        const QString v2 = makeTestVideo(m_tmp.path(), "b.mp4", 3.0, 480, 640, "24"); // vertical, other rate
        if (v1.isEmpty() || v2.isEmpty())
            QSKIP("FFmpeg not available");
        const QString png = m_tmp.path() + "/logo.png";
        QImage logo(200, 100, QImage::Format_ARGB32);
        logo.fill(QColor(0, 200, 120, 200));
        logo.save(png);

        Project p = makeEmptyProject("Full", CanvasSettings{1280, 720, {30, 1}, 48000, Qt::black});
        QString err;
        for (const QString &f : {v1, v2, png}) {
            MediaAsset m;
            m.id = newId();
            QVERIFY2(MediaProbe::probe(f, m, &err), qPrintable(err));
            p.media << m;
        }
        const QString v1t = p.tracks[0].id, v2t = p.tracks[1].id;
        const QString c1 = TimelineOps::addMediaClip(p, p.media[0].id, v1t, 0);
        const QString c2 = TimelineOps::addMediaClip(p, p.media[1].id, v1t, 4 * kUsPerSec);
        const QString c3 = TimelineOps::addMediaClip(p, p.media[2].id, v2t, kUsPerSec, false, 2 * kUsPerSec);
        QVERIFY(!c1.isEmpty() && !c2.isEmpty() && !c3.isEmpty());
        Clip *a = p.findClip(c1);
        a->color.saturation = 0.2;
        a->transform.cropL = 0.1;
        a->fadeIn = 500 * kUsPerMs;
        a->audio.fadeOut = 500 * kUsPerMs;
        a->audio.gainDb = -6;
        QVERIFY(TimelineOps::setClipSpeed(p, c2, 1.5));
        Clip *b = p.findClip(c2);
        b->transform.rotation = 7;
        b->transform.fit = "fill";
        Clip *l = p.findClip(c3);
        l->transform.scale = 0.3;
        l->transform.posX = 0.3;
        l->transform.posY = -0.3;
        l->transform.opacity = 0.8;
        l->fadeOut = 300 * kUsPerMs;

        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/full";
        req.settings.width = 1280;
        req.settings.height = 720;
        req.settings.fps = {30, 1};
        req.settings.encoder = "software";
        req.settings.speedPreset = "ultrafast";
        req.settings.outputPath = m_tmp.path() + "/full.mp4";
        req.overlay = OverlayScene::build(p);
        QVERIFY2(runExport(req, &err), qPrintable(err));
        const qint64 expectFrames = usToFrameRound(p.duration(), {30, 1});
        const QJsonObject info = probeJson(m_tmp.path() + "/full.mp4");
        QCOMPARE(info.value("streams").toArray()[0].toObject().value("nb_read_frames").toString().toLongLong(), expectFrames);
        const double dur = info.value("format").toObject().value("duration").toString().toDouble();
        QVERIFY(std::abs(dur - p.duration() / 1e6) < 0.1);

        // range export (1.0 s .. 3.0 s)
        req.settings.rangeStart = kUsPerSec;
        req.settings.rangeEnd = 3 * kUsPerSec;
        req.settings.outputPath = m_tmp.path() + "/range.mp4";
        QVERIFY2(runExport(req, &err), qPrintable(err));
        const QJsonObject rinfo = probeJson(m_tmp.path() + "/range.mp4");
        QCOMPARE(rinfo.value("streams").toArray()[0].toObject().value("nb_read_frames").toString().toInt(), 60);
    }

    void cancelLeavesNoPartialFile()
    {
        const QString v = makeTestVideo(m_tmp.path(), "long.mp4", 20.0, 1280, 720, "30");
        if (v.isEmpty())
            QSKIP("FFmpeg not available");
        Project p = makeEmptyProject("Cancel", CanvasSettings());
        MediaAsset m;
        m.id = "m";
        QString err;
        QVERIFY(MediaProbe::probe(v, m, &err));
        p.media << m;
        TimelineOps::addMediaClip(p, "m", {}, 0);
        ExportRequest req;
        req.project = p;
        req.projectDir = m_tmp.path() + "/cancel";
        req.settings.encoder = "software";
        req.settings.speedPreset = "slow";
        req.settings.outputPath = m_tmp.path() + "/cancel.mp4";
        ExportJob job(req);
        QSignalSpy cancelled(&job, &ExportJob::cancelledByUser);
        job.start();
        QTest::qWait(1500);
        job.cancel();
        QVERIFY(job.wait(20000));
        QCoreApplication::processEvents();
        QCOMPARE(cancelled.size(), 1);
        QVERIFY(!QFileInfo::exists(m_tmp.path() + "/cancel.mp4"));
        QVERIFY(!QFileInfo::exists(m_tmp.path() + "/cancel.partial.mp4"));
    }
};

QTEST_MAIN(TestRenderExport)
#include "tst_render_export.moc"
