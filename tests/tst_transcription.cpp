#include "TestUtil.h"

#include "captions/Transcription.h"

#include <QSignalSpy>

using namespace ss;
using namespace ss::test;

class TestTranscription : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

private slots:
    void initTestCase()
    {
        AppPaths::setRoot(m_tmp.path() + "/root");
        AppPaths::ensureLayout();
    }

    void prioritisedLanguagesExist()
    {
        for (const char *code : {"en", "hi", "hinglish", "pa", "auto"})
            QCOMPARE(Languages::byCode(code).code, QString(code));
        QVERIFY(!Languages::byCode("hinglish").prompt.isEmpty());
        QCOMPARE(Languages::byCode("pa").whisperLang, QString("pa"));
    }

    void wavReader()
    {
        if (Tools::ffmpeg().isEmpty())
            QSKIP("FFmpeg not available");
        const QString wav = m_tmp.path() + "/t.wav";
        Tools::run(Tools::ffmpeg(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                     "sine=f=300:d=1.5:sample_rate=16000", "-ac", "1", "-c:a", "pcm_s16le", wav});
        QByteArray pcm;
        int sr = 0, ch = 0;
        QString err;
        QVERIFY2(readWavPcm16(wav, pcm, &sr, &ch, &err), qPrintable(err));
        QCOMPARE(sr, 16000);
        QCOMPARE(ch, 1);
        QCOMPARE(pcm.size(), 16000 * 3);
    }

    void modelManagerImportVerifyAndTamperDetection()
    {
        const QString src = m_tmp.path() + "/ggml-fake.bin";
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("lmgg");
        f.write(QByteArray(24 << 20, 'm'));
        f.close();
        auto &mm = ModelManager::instance();
        QString err;
        // a web page saved with a .bin name is rejected with a clear message
        const QString html = m_tmp.path() + "/ggml-small.bin";
        QFile h(html);
        QVERIFY(h.open(QIODevice::WriteOnly));
        h.write("<!doctype html>\n<html><body>Hugging Face</body></html>");
        h.close();
        QVERIFY(!mm.importModel(html, "test", {}, &err));
        QVERIFY2(err.contains("web page"), qPrintable(err));
        // truncated real-format file is rejected too
        const QString tiny = m_tmp.path() + "/ggml-base.bin";
        QFile tf(tiny);
        QVERIFY(tf.open(QIODevice::WriteOnly));
        tf.write("lmgg");
        tf.write(QByteArray(1000, 'x'));
        tf.close();
        QVERIFY(!mm.importModel(tiny, "test", {}, &err));
        QVERIFY2(err.contains("incomplete"), qPrintable(err));
        QVERIFY(!mm.importModel(src, "test", "deadbeef", &err)); // wrong checksum
        QVERIFY(err.contains("Checksum"));
        const QString sha = ModelManager::sha256Of(src);
        QVERIFY2(mm.importModel(src, "unit test fixture", sha, &err), qPrintable(err));
        auto models = mm.speechModels();
        QCOMPARE(models.size(), 1);
        QCOMPARE(models[0].status, QString("verified"));
        QCOMPARE(models[0].provenance, QString("unit test fixture"));
        QVERIFY(mm.quickVerify("ggml-fake.bin", &err));
        // tamper with the installed file
        QFile t(mm.dir() + "/ggml-fake.bin");
        QVERIFY(t.open(QIODevice::ReadWrite));
        t.seek(100);
        t.write("X");
        t.close();
        QVERIFY(!mm.verify("ggml-fake.bin", &err));
        QVERIFY(mm.remove("ggml-fake.bin", &err));
        QVERIFY(mm.speechModels().isEmpty());
    }

    void bundledModelsAreFoundAndProtected()
    {
        const QString bdir = QCoreApplication::applicationDirPath() + "/models";
        QDir().mkpath(bdir);
        const QString path = bdir + "/ggml-bundledtest.bin";
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("lmgg");
            f.resize(21LL << 20);
        }
        const ModelInfo m = ModelManager::instance().find("ggml-bundledtest.bin");
        QVERIFY(m.bundled);
        QCOMPARE(m.status, QString("verified"));
        QString err;
        QVERIFY(ModelManager::instance().quickVerify("ggml-bundledtest.bin", &err));
        QVERIFY(!ModelManager::instance().remove("ggml-bundledtest.bin", &err));
        QVERIFY(QFileInfo::exists(path));
        QFile::remove(path);
        QDir(bdir).rmdir(".");
        QVERIFY(ModelManager::instance().find("ggml-bundledtest.bin").path.isEmpty());
    }

    void jobReportsMissingModelClearly()
    {
        TranscriptionJob job;
        QSignalSpy failed(&job, &TranscriptionJob::failed);
        TranscriptionRequest req;
        req.mediaPath = m_tmp.path() + "/none.wav";
        req.modelFile = "ggml-missing.bin";
        req.workDir = m_tmp.path() + "/work";
        job.start(req);
        QCOMPARE(failed.size(), 1);
        QVERIFY(!failed.first().at(0).toString().isEmpty());
    }

    // End-to-end with a real whisper.cpp binary and model when available:
    //   SHYFTR_TEST_MODEL=/path/ggml-base.bin SHYFTR_TEST_AUDIO=/path/jfk.wav
    void endToEndWithLocalWhisper()
    {
        const QString model = qEnvironmentVariable("SHYFTR_TEST_MODEL");
        const QString audio = qEnvironmentVariable("SHYFTR_TEST_AUDIO");
        if (model.isEmpty() || audio.isEmpty() || Tools::whisper().isEmpty() || Tools::ffmpeg().isEmpty())
            QSKIP("Set SHYFTR_TEST_MODEL and SHYFTR_TEST_AUDIO and install whisper-cli to run this test");
        QString err;
        if (ModelManager::instance().find(QFileInfo(model).fileName()).path.isEmpty())
            QVERIFY2(ModelManager::instance().importModel(model, "test", {}, &err), qPrintable(err));
        TranscriptionJob job;
        QSignalSpy done(&job, &TranscriptionJob::finished);
        QSignalSpy failed(&job, &TranscriptionJob::failed);
        QSignalSpy progress(&job, &TranscriptionJob::progress);
        TranscriptionRequest req;
        req.mediaId = "m";
        req.mediaPath = audio;
        req.mediaDuration = 11 * kUsPerSec;
        req.languageCode = qEnvironmentVariable("SHYFTR_TEST_LANG", "en");
        req.modelFile = QFileInfo(model).fileName();
        req.workDir = m_tmp.path() + "/e2e";
        req.styleForSegmentation = CaptionStyle();
        job.start(req);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty() || !failed.isEmpty(), 600000);
        if (!failed.isEmpty())
            QFAIL(qPrintable(failed.first().at(0).toString() + "\n" + failed.first().at(1).toString().right(2000)));
        const Transcript t = done.first().at(0).value<Transcript>();
        qInfo() << "summary:" << done.first().at(1).toString();
        QVERIFY(!progress.isEmpty());
        QVERIFY(t.words.size() >= 0);
        for (int i = 1; i < t.words.size(); ++i)
            QVERIFY(t.words[i].start >= t.words[i - 1].end);
    }
};

QTEST_GUILESS_MAIN(TestTranscription)
#include "tst_transcription.moc"
