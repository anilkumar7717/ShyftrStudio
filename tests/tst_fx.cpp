#include "TestUtil.h"
#include "captions/TextStyles.h"
#include "captions/OverlayRenderer.h"
#include "core/ProjectSerializer.h"
#include <QPainter>

#include "core/ProjectSerializer.h"
#include "media/Compositor.h"
#include "media/ImageFx.h"

#include <QGuiApplication>
#include <QPainter>

using namespace ss;
using namespace ss::test;

class TestFx : public QObject {
    Q_OBJECT
private slots:
    void keyframesInterpolateWithEasing()
    {
        QVector<Keyframe> k(2);
        k[0].t = 0;
        k[0].x = 0;
        k[0].ease = "linear";
        k[1].t = kUsPerSec;
        k[1].x = 1;
        k[1].opacity = 0;
        QCOMPARE(Keyframes::evaluate(k, -5).x, 0.0);
        QCOMPARE(Keyframes::evaluate(k, kUsPerSec / 2).x, 0.5);
        QCOMPARE(Keyframes::evaluate(k, 2 * kUsPerSec).x, 1.0);
        QVERIFY(std::abs(Keyframes::evaluate(k, kUsPerSec / 2).opacity - 0.5) < 1e-9);
        k[0].ease = "hold";
        QCOMPARE(Keyframes::evaluate(k, kUsPerSec * 9 / 10).x, 0.0);
        k[0].ease = "smooth";
        QVERIFY(Keyframes::evaluate(k, kUsPerSec / 10).x < 0.1);
    }

    void animatedClipMapsValuesPerType()
    {
        Clip media;
        media.fx.keyframes = {Keyframe{0, 0.1, 0.2, 1.5, 10, 0.5, "linear"}};
        const Clip m = media.animatedAt(0);
        QCOMPARE(m.transform.posX, 0.1);
        QCOMPARE(m.transform.scale, 1.5);
        Clip title;
        title.type = ClipType::Title;
        title.fx.keyframes = {Keyframe{0, 0.3, 0.7, 2, 0, 1, "linear"}};
        const Clip t = title.animatedAt(0);
        QCOMPARE(t.title.x, 0.3);
        QCOMPARE(t.title.y, 0.7);
        QCOMPARE(t.transform.scale, 2.0);
    }

    void speedCurveKeepsDurationAndIsMonotonic()
    {
        Clip c;
        c.in = 0;
        c.out = 10 * kUsPerSec;
        c.start = 2 * kUsPerSec;
        c.fx.speedCurve = SpeedCurve::fromPreset("hero");
        QCOMPARE(c.duration(), 10 * kUsPerSec);
        QCOMPARE(c.timelineToSource(c.start), c.in);
        QCOMPARE(c.timelineToSource(c.end()), c.out);
        TimeUs prev = -1;
        for (int i = 0; i <= 100; ++i) {
            const TimeUs t = c.start + c.duration() * i / 100;
            const TimeUs s = c.timelineToSource(t);
            QVERIFY(s >= prev);
            prev = s;
            QVERIFY(std::llabs(c.sourceToTimeline(s) - t) < 2000);
        }
        // the slow part in the middle consumes less source than the edges
        const TimeUs mid = c.timelineToSource(c.start + 5100 * kUsPerMs) - c.timelineToSource(c.start + 4900 * kUsPerMs);
        const TimeUs edge = c.timelineToSource(c.start + 1100 * kUsPerMs) - c.timelineToSource(c.start + 900 * kUsPerMs);
        QVERIFY(mid < edge / 2);
    }

    void splitKeepsKeyframedMotion()
    {
        Project p = makeEmptyProject("Split", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 10 * kUsPerSec, false);
        const QString id = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0, false);
        Clip *c = p.findClip(id);
        c->fx.keyframes = {Keyframe{0, 0, 0, 1, 0, 1, "linear"}, Keyframe{4 * kUsPerSec, 0.4, 0, 2, 0, 1, "linear"}};
        const Clip before = *c;
        QVERIFY(TimelineOps::splitClip(p, id, 2 * kUsPerSec));
        const Track &tr = p.tracks[0];
        QCOMPARE(tr.clips.size(), 2);
        for (TimeUs t : {TimeUs(500 * kUsPerMs), TimeUs(1900 * kUsPerMs), TimeUs(2000 * kUsPerMs), TimeUs(3 * kUsPerSec)}) {
            const Clip &half = tr.clips[t < 2 * kUsPerSec ? 0 : 1];
            QVERIFY2(std::abs(half.animatedAt(t).transform.posX - before.animatedAt(t).transform.posX) < 1e-6, qPrintable(QString::number(t)));
            QVERIFY(std::abs(half.animatedAt(t).transform.scale - before.animatedAt(t).transform.scale) < 1e-6);
        }
    }

    void splitKeepsEasedAndHoldKeyframes()
    {
        Project p = makeEmptyProject("Split2", CanvasSettings{1920, 1080, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 10 * kUsPerSec, false);
        const QString id = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0, false);
        Clip *c = p.findClip(id);
        c->fx.keyframes = {Keyframe{0, 0, 0, 1, 0, 1, "smooth"}, Keyframe{4 * kUsPerSec, 0.4, 0.2, 2, 30, 1, "hold"},
                           Keyframe{6 * kUsPerSec, -0.3, 0, 1, 0, 1, "linear"}};
        const Clip before = *c;
        QVERIFY(TimelineOps::splitClip(p, id, 1500 * kUsPerMs));
        QVERIFY(TimelineOps::splitClip(p, p.tracks[0].clips[1].id, 5 * kUsPerSec));
        const Track &tr = p.tracks[0];
        QCOMPARE(tr.clips.size(), 3);
        for (int i = 0; i <= 80; ++i) {
            const TimeUs t = i * 100 * kUsPerMs;
            const Clip *half = nullptr;
            for (const Clip &h : tr.clips)
                if (t >= h.start && t < h.end())
                    half = &h;
            QVERIFY(half);
            const auto a = half->animatedAt(t).transform, b = before.animatedAt(t).transform;
            QVERIFY2(std::abs(a.posX - b.posX) < 0.01 && std::abs(a.scale - b.scale) < 0.02 && std::abs(a.rotation - b.rotation) < 0.5,
                     qPrintable(QString("t=%1 %2 vs %3").arg(t).arg(a.posX).arg(b.posX)));
        }
    }

    void fxSurvivesSaveAndLoad()
    {
        Project p = makeEmptyProject("Fx", CanvasSettings{1280, 720, {30, 1}, 48000, Qt::black});
        p.media << fakeVideo("v", 10 * kUsPerSec, true);
        const QString id = TimelineOps::addMediaClip(p, "v", p.tracks[0].id, 0);
        Clip *c = p.findClip(id);
        c->fx.keyframes = {Keyframe{0, 0.1, 0.2, 1.3, 5, 0.9, "bounce"}};
        c->fx.transitionIn = {"slide-left", 700 * kUsPerMs};
        c->fx.effects = {EffectSpec{"glitch", 0.8, 2}, EffectSpec{"shake", 0.4, 1}};
        c->fx.chroma.enabled = true;
        c->fx.chroma.color = QColor(0, 0, 255);
        c->fx.mask = MaskSpec{"circle", 0.4, 0.5, 0.3, 0.4, 15, 0.1, 0.1, true};
        c->fx.blend = "screen";
        c->fx.background.mode = "blur";
        c->fx.adjust.hue = 30;
        c->fx.adjust.vignette = 0.4;
        c->fx.speedCurve = SpeedCurve::fromPreset("montage");
        c->fx.audio.voice = "robot";
        c->fx.audio.denoise = true;
        const QString audioTrack = p.tracks[2].id;
        p.tracks[2].duck = true;
        p.markers << Marker{3 * kUsPerSec, "Drop", QColor("#ff0000")};
        const QString tid = TimelineOps::addTitleClip(p, {}, 0, kUsPerSec, TitleProps());
        Clip *t = p.findClip(tid);
        t->title.loop = "pulse";
        t->title.animationOut = "zoom";
        t->title.gradientColor = QColor("#ff00ff");
        t->title.shadowColor = QColor(0, 0, 0, 128);
        const QString eid = TimelineOps::addEffectClip(p, {}, 0, kUsPerSec, ClipFx{}, "Glow");
        QVERIFY(!eid.isEmpty());

        Project q;
        QString err;
        QVERIFY2(ProjectSerializer::fromBytes(ProjectSerializer::toBytes(p, false), q, &err), qPrintable(err));
        const Clip *qc = q.findClip(id);
        QCOMPARE(qc->fx.keyframes.size(), 1);
        QCOMPARE(qc->fx.keyframes[0].ease, QString("bounce"));
        QCOMPARE(qc->fx.transitionIn.type, QString("slide-left"));
        QCOMPARE(qc->fx.effects.size(), 2);
        QCOMPARE(qc->fx.effects[0].speed, 2.0);
        QVERIFY(qc->fx.chroma.enabled);
        QCOMPARE(qc->fx.chroma.color, QColor(0, 0, 255));
        QVERIFY(qc->fx.mask.invert);
        QCOMPARE(qc->fx.mask.angle, 15.0);
        QCOMPARE(qc->fx.blend, QString("screen"));
        QCOMPARE(qc->fx.background.mode, QString("blur"));
        QCOMPARE(qc->fx.adjust.hue, 30.0);
        QCOMPARE(qc->fx.speedCurve.points.size(), 6);
        QCOMPARE(qc->fx.audio.voice, QString("robot"));
        QVERIFY(q.findTrack(audioTrack)->duck);
        QCOMPARE(q.markers.size(), 1);
        QCOMPARE(q.findClip(tid)->title.loop, QString("pulse"));
        QCOMPARE(q.findClip(tid)->title.gradientColor, QColor("#ff00ff"));
        QVERIFY(q.findClip(eid)->type == ClipType::Effect);
        QVERIFY(Compositor::projectNeedsCompositor(q));
    }

    void chromaKeyRemovesGreenKeepsSubject()
    {
        QImage img(100, 50, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0, 255, 0));
        QPainter(&img).fillRect(40, 10, 20, 30, QColor(220, 120, 90)); // skin-ish subject
        ChromaKey ck;
        ck.enabled = true;
        ImageFx::chromaKey(img, ck);
        QCOMPARE(qAlpha(img.pixel(5, 5)), 0);
        QCOMPARE(qAlpha(img.pixel(50, 20)), 255);
    }

    void masksCutTheRightArea()
    {
        QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        MaskSpec m;
        m.type = "circle";
        m.w = 0.4;
        m.h = 0.8;
        m.feather = 0;
        QImage a = img.copy();
        ImageFx::applyMask(a, m);
        QCOMPARE(qAlpha(a.pixel(100, 50)), 255);
        QCOMPARE(qAlpha(a.pixel(5, 5)), 0);
        m.invert = true;
        QImage b = img.copy();
        ImageFx::applyMask(b, m);
        QCOMPARE(qAlpha(b.pixel(100, 50)), 0);
        QCOMPARE(qAlpha(b.pixel(5, 5)), 255);
        m = MaskSpec();
        m.type = "linear";
        m.feather = 0;
        QImage c = img.copy();
        ImageFx::applyMask(c, m);
        QCOMPARE(qAlpha(c.pixel(100, 10)), 0);   // above the line
        QCOMPARE(qAlpha(c.pixel(100, 90)), 255); // below the line
    }

    void everyEffectAndTransitionRuns()
    {
        QImage base(160, 90, QImage::Format_ARGB32_Premultiplied);
        QLinearGradient g(0, 0, 160, 90);
        g.setColorAt(0, Qt::red);
        g.setColorAt(1, Qt::blue);
        QPainter(&base).fillRect(base.rect(), g);
        for (const auto &e : ImageFx::effects()) {
            QImage img = base.copy();
            EffectSpec es;
            es.type = e.id;
            es.intensity = 1.0;
            ImageFx::applyEffect(img, es, 0.37, 0.5, 42);
            QCOMPARE(img.size(), base.size());
            if (!ImageFx::isMotionEffect(e.id) && e.id != "flash" && e.id != "strobe")
                QVERIFY2(img != base, qPrintable(e.id));
        }
        QImage other(base.size(), QImage::Format_ARGB32_Premultiplied);
        other.fill(Qt::yellow);
        for (const auto &tr : ImageFx::transitions()) {
            QImage out(base.size(), QImage::Format_ARGB32_Premultiplied);
            out.fill(Qt::black);
            {
                QPainter p(&out);
                ImageFx::drawTransition(p, base, other, tr.id, 0.0, 1);
            }
            QImage end(base.size(), QImage::Format_ARGB32_Premultiplied);
            end.fill(Qt::black);
            {
                QPainter p(&end);
                ImageFx::drawTransition(p, base, other, tr.id, 1.0, 1);
            }
            // starts on A and finishes on B
            QVERIFY2(qAbs(qRed(end.pixel(80, 45)) - 255) < 20 && qAbs(qGreen(end.pixel(80, 45)) - 255) < 20, qPrintable(tr.id));
            QVERIFY2(qAbs(qRed(out.pixel(80, 45)) - qRed(base.pixel(80, 45))) < 30, qPrintable(tr.id));
        }
    }

    void textStylesChangeOnlyTheLook()
    {
        TitleProps tp;
        tp.text = "Hello *world*";
        tp.x = 0.3;
        tp.y = 0.7;
        tp.sizePct = 0.06;
        tp.align = "left";
        tp.animation = "pop";
        QVERIFY(TextStyles::all().size() >= 24);
        QSet<QString> ids;
        for (const TextStyle &st : TextStyles::all()) {
            QVERIFY2(!ids.contains(st.id), qPrintable(st.id));
            ids.insert(st.id);
            TitleProps t = tp;
            QVERIFY(TextStyles::apply(t, st.id));
            QCOMPARE(t.text, tp.text);
            QCOMPARE(t.x, tp.x);
            QCOMPARE(t.y, tp.y);
            QCOMPARE(t.sizePct, tp.sizePct);
            QCOMPARE(t.align, tp.align);
            QCOMPARE(t.animation, tp.animation);
            QCOMPARE(t.styleId, st.id);
            // every style draws visible text
            Clip c;
            c.type = ClipType::Title;
            c.out = 4 * kUsPerSec;
            c.title = t;
            c.title.animation = "none";
            QImage img(640, 360, QImage::Format_ARGB32_Premultiplied);
            img.fill(QColor(40, 40, 40));
            QPainter pa(&img);
            GraphicsRenderer::paintTitle(pa, img.size(), c, kUsPerSec);
            pa.end();
            int changed = 0;
            for (int y = 0; y < 360; y += 3)
                for (int x = 0; x < 640; x += 3)
                    changed += img.pixel(x, y) != qRgb(40, 40, 40);
            QVERIFY2(changed > 40, qPrintable(st.id));
        }
        // switching styles does not keep leftovers from the previous one (e.g. a box)
        TitleProps t = tp;
        TextStyles::apply(t, "box-dark");
        QVERIFY(t.backgroundColor.alpha() > 0);
        TextStyles::apply(t, "clean-white");
        QCOMPARE(t.backgroundColor.alpha(), 0);
        QVERIFY(!TextStyles::apply(t, "nope"));
    }

    void textFormattingRenders()
    {
        auto render = [](const TitleProps &tp) {
            Clip c;
            c.type = ClipType::Title;
            c.out = 4 * kUsPerSec;
            c.title = tp;
            c.title.animation = "none";
            QImage img(640, 360, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::black);
            QPainter pa(&img);
            GraphicsRenderer::paintTitle(pa, img.size(), c, kUsPerSec);
            return img;
        };
        TitleProps tp;
        tp.text = "plain *GOLD*";
        tp.color = Qt::white;
        tp.highlightColor = QColor(255, 200, 0);
        tp.shadowColor = QColor(0, 0, 0, 0);
        const QImage a = render(tp);
        int yellow = 0, white = 0;
        for (int y = 0; y < 360; ++y)
            for (int x = 0; x < 640; ++x) {
                const QColor c = a.pixelColor(x, y);
                yellow += c.red() > 200 && c.green() > 150 && c.blue() < 60;
                white += c.red() > 200 && c.green() > 200 && c.blue() > 200;
            }
        QVERIFY(yellow > 200); // *GOLD* in the highlight colour
        QVERIFY(white > 200);  // the rest in the text colour
        // stars are not drawn: same width as the text without them
        Clip c1, c2;
        c1.type = c2.type = ClipType::Title;
        c1.title = tp;
        c2.title = tp;
        c2.title.text = "plain GOLD";
        QVERIFY(std::abs(GraphicsRenderer::titleRect(QSize(640, 360), c1).width() - GraphicsRenderer::titleRect(QSize(640, 360), c2).width()) < 1);
        // letter case
        TitleProps up = tp;
        up.text = "make me big";
        up.textCase = "upper";
        QCOMPARE(up.displayText(), QString("MAKE ME BIG"));
        up.textCase = "title";
        QCOMPARE(up.displayText(), QString("Make Me Big"));
        // line spacing makes a two-line block taller
        Clip l1, l2;
        l1.type = l2.type = ClipType::Title;
        l1.title.text = l2.title.text = "one\ntwo";
        l2.title.lineSpacing = 1.8;
        QVERIFY(GraphicsRenderer::titleRect(QSize(640, 360), l2).height() > GraphicsRenderer::titleRect(QSize(640, 360), l1).height() * 1.4);
        // underline draws below the text
        TitleProps u;
        u.text = "under";
        u.shadowColor = QColor(0, 0, 0, 0);
        const QImage noU = render(u);
        u.underline = true;
        const QImage withU = render(u);
        QVERIFY(noU != withU);
        // formatting survives save/load
        Project p = makeEmptyProject("T", CanvasSettings());
        TitleProps f = tp;
        f.underline = true;
        f.textCase = "upper";
        f.lineSpacing = 1.4;
        f.glowColor = QColor("#22D3EE");
        f.styleId = "neon-cyan";
        const QString id = TimelineOps::addTitleClip(p, {}, 0, kUsPerSec, f);
        Project q;
        QString err;
        QVERIFY(ProjectSerializer::fromBytes(ProjectSerializer::toBytes(p), q, &err));
        const TitleProps &g = q.findClip(id)->title;
        QVERIFY(g.underline);
        QCOMPARE(g.textCase, QString("upper"));
        QCOMPARE(g.lineSpacing, 1.4);
        QCOMPARE(g.glowColor, QColor("#22D3EE"));
        QCOMPARE(g.highlightColor, tp.highlightColor);
        QCOMPARE(g.styleId, QString("neon-cyan"));
    }

    void looksAndAdjustRun()
    {
        QImage base(64, 64, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(120, 140, 160));
        for (const auto &l : ImageFx::looks()) {
            QImage img = base.copy();
            ImageFx::colorAdjust(img, l.adjust, 1);
            QCOMPARE(img.size(), base.size());
        }
        ExtraAdjust a;
        a.hue = 180;
        QImage img = base.copy();
        ImageFx::colorAdjust(img, a, 1);
        QVERIFY(qRed(img.pixel(10, 10)) > qBlue(img.pixel(10, 10))); // hue flipped blue-ish to orange-ish
    }

    void lutIdentityKeepsColours()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/id.cube";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ts(&f);
        ts << "TITLE \"identity\"\nLUT_3D_SIZE 2\n";
        for (int b = 0; b < 2; ++b)
            for (int g = 0; g < 2; ++g)
                for (int r = 0; r < 2; ++r)
                    ts << r << " " << g << " " << b << "\n";
        ts.flush();
        f.close();
        QImage img(8, 8, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(30, 100, 200));
        QString err;
        QVERIFY2(ImageFx::applyLut(img, path, 1.0, &err), qPrintable(err));
        QVERIFY(std::abs(qRed(img.pixel(1, 1)) - 30) <= 1);
        QVERIFY(std::abs(qBlue(img.pixel(1, 1)) - 200) <= 1);
    }

    void audioFxChains()
    {
        AudioFx a;
        QVERIFY(a.ffmpegChain(48000).isEmpty());
        for (const QString &v : AudioFx::voiceNames()) {
            a.voice = v;
            QVERIFY2(!a.ffmpegChain(48000).isEmpty(), qPrintable(v));
        }
        a.voice = "chipmunk";
        QVERIFY(a.ffmpegChain(48000).contains("asetrate=72000"));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestFx t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_fx.moc"
