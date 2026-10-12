#include "Transcription.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "CaptionEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStorageInfo>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace ss {

// =================================================================== Languages

QVector<LanguageOption> Languages::all()
{
    return {
        {"auto", "Auto-detect", "auto", {}, "Detection is reliable for longer, clear speech; pick the language for short clips."},
        {"en", "English", "en", {}, {}},
        {"hi", "Hindi (Devanagari)", "hi", "नमस्ते, आज हम बात करेंगे।", "Outputs Devanagari script."},
        {"hinglish", "Hinglish (Roman script)", "hi",
         "Haan bhai, aaj hum baat karenge. Toh chalo shuru karte hain, theek hai? This is really important.",
         "Hindi/English code-switching written in Latin script. The prompt nudges Roman output; evaluate on your "
         "recordings and correct names in the editor."},
        {"pa", "Punjabi (Gurmukhi)", "pa", "ਸਤ ਸ੍ਰੀ ਅਕਾਲ ਜੀ, ਅੱਜ ਅਸੀਂ ਗੱਲ ਕਰਾਂਗੇ।", "Outputs Gurmukhi script. Accuracy varies by model size; use small or larger."},
        {"ur", "Urdu", "ur", {}, {}},
        {"bn", "Bengali", "bn", {}, {}},
        {"mr", "Marathi", "mr", {}, {}},
        {"gu", "Gujarati", "gu", {}, {}},
        {"ta", "Tamil", "ta", {}, {}},
        {"te", "Telugu", "te", {}, {}},
        {"es", "Spanish", "es", {}, {}},
        {"fr", "French", "fr", {}, {}},
        {"de", "German", "de", {}, {}},
        {"ar", "Arabic", "ar", {}, {}},
    };
}

LanguageOption Languages::byCode(const QString &code)
{
    for (const auto &l : all())
        if (l.code == code)
            return l;
    return all().first();
}

// =================================================================== ModelManager

ModelManager &ModelManager::instance()
{
    static ModelManager m;
    return m;
}

QString ModelManager::dir() const
{
    return AppPaths::modelsDir();
}

QVector<ModelManager::CatalogEntry> ModelManager::catalog()
{
    return {
        {"ggml-tiny", 75LL << 20, "~0.4 GB", "Fastest; rough accuracy. Drafts only."},
        {"ggml-base", 142LL << 20, "~0.5 GB", "Fast; fine for clear English."},
        {"ggml-small", 466LL << 20, "~1 GB", "Recommended default for Hindi, Hinglish and Punjabi."},
        {"ggml-medium", 1500LL << 20, "~2.6 GB", "Higher accuracy; slower on CPU."},
        {"ggml-large-v3-turbo", 1620LL << 20, "~3 GB", "High accuracy with good speed on Apple Silicon / GPU."},
        {"ggml-large-v3", 3100LL << 20, "~4.7 GB", "Highest accuracy; needs 16 GB RAM recommended."},
        {"ggml-silero-v5.1.2", 1LL << 20, "small", "Optional voice-activity-detection model (--vad)."},
    };
}

QString ModelManager::describeSize(qint64 bytes)
{
    if (bytes >= (1LL << 30))
        return QString::number(bytes / double(1LL << 30), 'f', 2) + " GB";
    if (bytes >= (1LL << 20))
        return QString::number(bytes / double(1LL << 20), 'f', 0) + " MB";
    return QString::number(bytes / 1024.0, 'f', 0) + " KB";
}

QVector<ModelInfo> ModelManager::loadManifest() const
{
    QVector<ModelInfo> out;
    QJsonObject o;
    if (!readJson(dir() + "/manifest.json", o))
        return out;
    for (const QJsonValue &v : o.value("models").toArray()) {
        const QJsonObject m = v.toObject();
        ModelInfo mi;
        mi.file = m.value("file").toString();
        mi.name = m.value("name").toString();
        mi.size = m.value("size").toInteger();
        mi.sha256 = m.value("sha256").toString();
        mi.mtime = QDateTime::fromString(m.value("mtime").toString(), Qt::ISODate);
        mi.addedAt = QDateTime::fromString(m.value("addedAt").toString(), Qt::ISODate);
        mi.provenance = m.value("provenance").toString();
        mi.license = m.value("license").toString();
        out << mi;
    }
    return out;
}

bool ModelManager::saveManifest(const QVector<ModelInfo> &models) const
{
    QJsonArray arr;
    for (const auto &m : models) {
        QJsonObject o;
        o["file"] = m.file;
        o["name"] = m.name;
        o["size"] = m.size;
        o["sha256"] = m.sha256;
        o["mtime"] = m.mtime.toString(Qt::ISODate);
        o["addedAt"] = m.addedAt.toString(Qt::ISODate);
        o["provenance"] = m.provenance;
        o["license"] = m.license;
        arr.append(o);
    }
    QJsonObject root;
    root["models"] = arr;
    root["note"] = "Local model inventory. SHA-256 is recorded when a model is imported or first verified.";
    return writeJsonAtomically(dir() + "/manifest.json", root);
}

QVector<ModelInfo> ModelManager::scan()
{
    QDir().mkpath(dir());
    QVector<ModelInfo> manifest = loadManifest();
    QVector<ModelInfo> out;
    QDir d(dir());
    const auto files = d.entryInfoList({"*.bin", "*.gguf"}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        ModelInfo mi;
        bool known = false;
        for (const auto &m : manifest)
            if (m.file == fi.fileName()) {
                mi = m;
                known = true;
            }
        mi.file = fi.fileName();
        mi.path = fi.absoluteFilePath();
        if (mi.name.isEmpty())
            mi.name = fi.completeBaseName();
        mi.isVad = mi.file.contains("silero", Qt::CaseInsensitive) || mi.file.contains("vad", Qt::CaseInsensitive);
        if (!known) {
            mi.status = "unverified";
            mi.size = fi.size();
            mi.addedAt = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
            mi.provenance = "Found in AI_Models folder";
        } else if (mi.size != fi.size() ||
                   (mi.mtime.isValid() && mi.mtime.toSecsSinceEpoch() != fi.lastModified().toSecsSinceEpoch())) {
            mi.status = "changed";
        } else {
            mi.status = mi.sha256.isEmpty() ? "unverified" : "verified";
        }
        QString formatErr;
        if (!checkModelFile(mi.path, &formatErr))
            mi.status = "invalid";
        out << mi;
    }
    return out;
}

QVector<ModelInfo> ModelManager::speechModels()
{
    QVector<ModelInfo> out;
    for (const auto &m : scan())
        if (!m.isVad)
            out << m;
    return out;
}

QVector<ModelInfo> ModelManager::vadModels()
{
    QVector<ModelInfo> out;
    for (const auto &m : scan())
        if (m.isVad)
            out << m;
    return out;
}

ModelInfo ModelManager::find(const QString &file)
{
    for (const auto &m : scan())
        if (m.file == file)
            return m;
    return {};
}

bool ModelManager::checkModelFile(const QString &path, QString *error)
{
    QString scratch;
    if (!error)
        error = &scratch;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = "The model file could not be opened: " + path;
        return false;
    }
    const QByteArray head = f.read(64);
    const qint64 size = f.size();
    const QString name = QFileInfo(path).fileName();
    const bool isVad = name.contains("silero", Qt::CaseInsensitive) || name.contains("vad", Qt::CaseInsensitive);
    if (head.startsWith("lmgg") || head.startsWith("GGUF")) {
        if (!isVad && size < (20LL << 20)) {
            if (error)
                *error = QString("%1 is only %2 — it looks like an incomplete download. Whisper models are 75 MB "
                                 "(tiny) to 3 GB (large). Download it again.")
                             .arg(name, describeSize(size));
            return false;
        }
        return true;
    }
    const QByteArray t = head.trimmed().toLower();
    if (t.startsWith("<!doctype") || t.startsWith("<html") || t.startsWith("<"))
        *error = QString("%1 is a web page (%2), not a speech model. This happens when the model is saved from the "
                         "website instead of downloaded directly. Run ./scripts/get-model.sh small, or download "
                         "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin")
                     .arg(name, describeSize(size));
    else if (t.startsWith("{") || t.startsWith("version https://git-lfs"))
        *error = QString("%1 is a text/pointer file (%2), not the model itself. Download the real model file.")
                     .arg(name, describeSize(size));
    else
        *error = QString("%1 is not a whisper.cpp model (unrecognised file format). Use a ggml-*.bin file from "
                         "huggingface.co/ggerganov/whisper.cpp.")
                     .arg(name);
    return false;
}

QString ModelManager::sha256Of(const QString &path, const std::function<bool(qint64, qint64)> &progress)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    const qint64 total = f.size();
    qint64 done = 0;
    QByteArray buf;
    while (!(buf = f.read(8 << 20)).isEmpty()) {
        h.addData(buf);
        done += buf.size();
        if (progress && !progress(done, total))
            return {};
    }
    return QString::fromLatin1(h.result().toHex());
}

bool ModelManager::importModel(const QString &src, const QString &provenance, const QString &expectedSha,
                               QString *error, const std::function<bool(qint64, qint64)> &progress)
{
    QFileInfo fi(src);
    if (!fi.isFile()) {
        if (error)
            *error = "Model file not found: " + src;
        return false;
    }
    if (!checkModelFile(src, error))
        return false;
    QDir().mkpath(dir());
    const QString dest = dir() + "/" + fi.fileName();
    if (QFileInfo::exists(dest)) {
        if (error)
            *error = "A model with this file name is already installed.";
        return false;
    }
    QStorageInfo si(dir());
    if (si.isValid() && si.bytesAvailable() < fi.size() + (256LL << 20)) {
        if (error)
            *error = QString("Not enough disk space: the model needs %1, %2 free.")
                         .arg(describeSize(fi.size()), describeSize(si.bytesAvailable()));
        return false;
    }
    QFile in(src);
    QFile out(dest + ".partial");
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = "Could not copy the model file.";
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    qint64 done = 0;
    QByteArray buf;
    while (!(buf = in.read(8 << 20)).isEmpty()) {
        h.addData(buf);
        if (out.write(buf) != buf.size()) {
            out.remove();
            if (error)
                *error = "Write failed while copying the model.";
            return false;
        }
        done += buf.size();
        if (progress && !progress(done, fi.size())) {
            out.close();
            out.remove();
            if (error)
                *error = "Cancelled";
            return false;
        }
    }
    out.close();
    const QString sha = QString::fromLatin1(h.result().toHex());
    if (!expectedSha.trimmed().isEmpty() && expectedSha.trimmed().compare(sha, Qt::CaseInsensitive) != 0) {
        QFile::remove(dest + ".partial");
        if (error)
            *error = "Checksum mismatch. Expected " + expectedSha.trimmed() + " but the file is " + sha + ".";
        return false;
    }
    if (!QFile::rename(dest + ".partial", dest)) {
        QFile::remove(dest + ".partial");
        if (error)
            *error = "Could not finalise the model file.";
        return false;
    }
    QVector<ModelInfo> manifest = loadManifest();
    ModelInfo mi;
    mi.file = fi.fileName();
    mi.name = fi.completeBaseName();
    mi.size = fi.size();
    mi.sha256 = sha;
    mi.mtime = QFileInfo(dest).lastModified().toUTC();
    mi.addedAt = QDateTime::currentDateTimeUtc();
    mi.provenance = provenance.isEmpty() ? "Imported from " + src : provenance;
    mi.license = mi.file.contains("ggml") ? "Whisper model weights: MIT (OpenAI); verify for your distribution" : QString();
    manifest << mi;
    return saveManifest(manifest);
}

bool ModelManager::verify(const QString &file, QString *error, const std::function<bool(qint64, qint64)> &progress)
{
    const QString path = dir() + "/" + file;
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = "Model file is missing.";
        return false;
    }
    const QString sha = sha256Of(path, progress);
    if (sha.isEmpty()) {
        if (error)
            *error = "Could not read the model file.";
        return false;
    }
    QVector<ModelInfo> manifest = loadManifest();
    for (auto &m : manifest)
        if (m.file == file) {
            if (!m.sha256.isEmpty() && m.sha256 != sha) {
                if (error)
                    *error = "The model file does not match the checksum recorded when it was installed. "
                             "Re-import it from a trusted copy.";
                return false;
            }
            m.sha256 = sha;
            m.size = QFileInfo(path).size();
            m.mtime = QFileInfo(path).lastModified().toUTC();
            return saveManifest(manifest);
        }
    ModelInfo mi;
    mi.file = file;
    mi.name = QFileInfo(file).completeBaseName();
    mi.size = QFileInfo(path).size();
    mi.sha256 = sha;
    mi.mtime = QFileInfo(path).lastModified().toUTC();
    mi.addedAt = QDateTime::currentDateTimeUtc();
    mi.provenance = "Placed in AI_Models manually; checksum recorded on first verification";
    manifest << mi;
    return saveManifest(manifest);
}

bool ModelManager::quickVerify(const QString &file, QString *error)
{
    const ModelInfo mi = find(file);
    if (mi.path.isEmpty()) {
        if (error)
            *error = "The selected speech model is not installed. Open Preferences → AI Models to add one.";
        return false;
    }
    if (!checkModelFile(mi.path, error)) {
        if (error)
            *error += "\n\nRemove it in Preferences → AI Models, then add a valid model.";
        return false;
    }
    if (mi.status == "verified")
        return true;
    return verify(file, error);
}

bool ModelManager::remove(const QString &file, QString *error)
{
    const QString path = dir() + "/" + file;
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        if (error)
            *error = "Could not delete " + path;
        return false;
    }
    QVector<ModelInfo> manifest = loadManifest();
    for (int i = manifest.size() - 1; i >= 0; --i)
        if (manifest[i].file == file)
            manifest.removeAt(i);
    return saveManifest(manifest);
}

// =================================================================== WAV

bool readWavPcm16(const QString &path, QByteArray &pcm, int *sampleRate, int *channels, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray head = f.read(12);
    if (head.size() < 12 || !head.startsWith("RIFF") || head.mid(8, 4) != "WAVE") {
        if (error)
            *error = "Not a WAV file";
        return false;
    }
    int sr = 0, ch = 0, bits = 0;
    while (!f.atEnd()) {
        const QByteArray ck = f.read(8);
        if (ck.size() < 8)
            break;
        const quint32 len = quint32(uchar(ck[4])) | quint32(uchar(ck[5])) << 8 | quint32(uchar(ck[6])) << 16 |
                            quint32(uchar(ck[7])) << 24;
        const QByteArray id = ck.left(4);
        if (id == "fmt ") {
            const QByteArray fmt = f.read(len);
            if (fmt.size() >= 16) {
                ch = uchar(fmt[2]) | uchar(fmt[3]) << 8;
                sr = int(quint32(uchar(fmt[4])) | quint32(uchar(fmt[5])) << 8 | quint32(uchar(fmt[6])) << 16 |
                         quint32(uchar(fmt[7])) << 24);
                bits = uchar(fmt[14]) | uchar(fmt[15]) << 8;
            }
            if (len % 2)
                f.read(1);
        } else if (id == "data") {
            const qint64 avail = f.size() - f.pos();
            const qint64 n = (len == 0xFFFFFFFF || len == 0 || len > avail) ? avail : len; // streamed WAVs
            pcm = f.read(n);
            break;
        } else {
            f.seek(f.pos() + len + (len % 2));
        }
    }
    if (bits != 16 || pcm.isEmpty()) {
        if (error)
            *error = "Unsupported WAV (expected 16-bit PCM)";
        return false;
    }
    if (sampleRate)
        *sampleRate = sr;
    if (channels)
        *channels = ch;
    return true;
}

// =================================================================== Job

TranscriptionJob::TranscriptionJob(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<ss::Transcript>("ss::Transcript");
}

TranscriptionJob::~TranscriptionJob()
{
    m_cancelled = true;
    cleanupProcess();
    if (m_watcher)
        m_watcher->waitForFinished();
}

QString TranscriptionJob::stageLabel(Stage s)
{
    switch (s) {
    case Stage::Idle: return "Idle";
    case Stage::Extracting: return "Extracting audio";
    case Stage::DetectingSpeech: return "Detecting speech";
    case Stage::Transcribing: return "Transcribing on this computer";
    case Stage::Aligning: return "Aligning word timing";
    case Stage::Segmenting: return "Building captions";
    case Stage::Done: return "Done";
    case Stage::Failed: return "Failed";
    case Stage::Cancelled: return "Cancelled";
    }
    return {};
}

bool TranscriptionJob::isRunning() const
{
    return m_stage != Stage::Idle && m_stage != Stage::Done && m_stage != Stage::Failed && m_stage != Stage::Cancelled;
}

void TranscriptionJob::setStage(Stage s)
{
    m_stage = s;
    m_log += QString("[%1] %2\n").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), stageLabel(s));
    emit stageChanged(s, stageLabel(s));
}

void TranscriptionJob::fail(const QString &msg)
{
    cleanupProcess();
    m_log += "ERROR: " + msg + "\n";
    setStage(Stage::Failed);
    emit failed(msg, m_log);
}

void TranscriptionJob::cleanupProcess()
{
    if (m_proc) {
        m_proc->disconnect(this);
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->kill();
            m_proc->waitForFinished(3000);
        }
        m_proc->deleteLater();
    }
    m_proc = nullptr;
}

void TranscriptionJob::cancel()
{
    if (!isRunning())
        return;
    m_cancelled = true;
    cleanupProcess();
    setStage(Stage::Cancelled);
    emit cancelled();
}

void TranscriptionJob::start(const TranscriptionRequest &req)
{
    if (isRunning())
        return;
    m_req = req;
    m_log.clear();
    m_cancelled = false;
    m_stderrBuf.clear();
    if (Tools::ffmpeg().isEmpty())
        return fail("FFmpeg was not found. Install it or set its location in Preferences → Tools.");
    if (Tools::whisper().isEmpty())
        return fail("The local speech engine (whisper.cpp 'whisper-cli') was not found. Install it or set its "
                    "location in Preferences → Tools.");
    QString err;
    if (!ModelManager::instance().quickVerify(req.modelFile, &err))
        return fail(err);
    if (!QFileInfo::exists(req.mediaPath))
        return fail("The source media file is missing. Relink it and try again.");
    QDir().mkpath(req.workDir);
    m_wavPath = req.workDir + "/audio-16k.wav";
    m_jsonBase = req.workDir + "/whisper-result";
    runExtract();
}

void TranscriptionJob::runExtract()
{
    setStage(Stage::Extracting);
    emit progress(0);
    m_proc = new QProcess(this);
    const QStringList args{"-hide_banner", "-nostdin", "-y", "-i", m_req.mediaPath, "-vn", "-ac", "1", "-ar", "16000",
                           "-c:a", "pcm_s16le", "-progress", "pipe:1", "-nostats", m_wavPath};
    m_log += "ffmpeg " + args.join(' ') + "\n";
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromUtf8(m_proc->readAllStandardOutput());
        for (const QString &line : out.split('\n'))
            if (line.startsWith("out_time_us=") && m_req.mediaDuration > 0) {
                const qint64 us = line.mid(12).toLongLong();
                emit progress(int(std::clamp(us * 10 / m_req.mediaDuration, qint64(0), qint64(10))));
            }
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() { m_log += QString::fromUtf8(m_proc->readAllStandardError()); });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        if (m_cancelled)
            return;
        if (st != QProcess::NormalExit || code != 0 || !QFileInfo::exists(m_wavPath))
            return fail("Could not extract audio from the media (does it contain an audio track?).");
        m_proc->deleteLater();
        m_proc = nullptr;
        runWhisper();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && !m_cancelled)
            fail("FFmpeg could not be started.");
    });
    m_proc->start(Tools::ffmpeg(), args);
}

void TranscriptionJob::runWhisper()
{
    setStage(Stage::Transcribing);
    emit progress(10);
    const LanguageOption lang = Languages::byCode(m_req.languageCode);
    const ModelInfo model = ModelManager::instance().find(m_req.modelFile);
    QStringList args{"-m", model.path, "-f", m_wavPath, "-l", lang.whisperLang, "-ojf", "-of", m_jsonBase, "-pp"};
    const int threads = m_req.threads > 0 ? m_req.threads : std::clamp(QThread::idealThreadCount() - 1, 1, 8);
    args << "-t" << QString::number(threads);
    if (!m_req.useGpu)
        args << "-ng";
    QString prompt = lang.prompt;
    if (!m_req.extraPrompt.trimmed().isEmpty())
        prompt = (prompt + " " + m_req.extraPrompt.trimmed()).trimmed();
    if (!prompt.isEmpty())
        args << "--prompt" << prompt;
    if (!m_req.vadModelFile.isEmpty()) {
        const ModelInfo vad = ModelManager::instance().find(m_req.vadModelFile);
        if (!vad.path.isEmpty())
            args << "--vad" << "-vm" << vad.path;
    }
    m_log += "whisper " + args.join(' ') + "\n";
    QFile::remove(m_jsonBase + ".json");

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray chunk = m_proc->readAllStandardError();
        m_stderrBuf += chunk;
        if (m_stderrBuf.size() > 200000)
            m_stderrBuf = m_stderrBuf.right(100000);
        const int p = WhisperParser::parseProgress(chunk);
        if (p >= 0)
            emit progress(10 + p * 80 / 100);
    });
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() { m_proc->readAllStandardOutput(); });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        if (m_cancelled)
            return;
        m_log += QString::fromUtf8(m_stderrBuf.right(20000));
        if (st != QProcess::NormalExit || code != 0 || !QFileInfo::exists(m_jsonBase + ".json")) {
            QString hint = "The speech engine stopped unexpectedly.";
            if (m_stderrBuf.contains("failed to load model") || m_stderrBuf.contains("invalid model"))
                hint = "The speech model could not be loaded. It may be damaged or incompatible with this whisper.cpp "
                       "version.";
            else if (m_stderrBuf.contains("unknown argument") || m_stderrBuf.contains("error: unknown"))
                hint = "This whisper.cpp build does not support a required option. Update whisper.cpp (1.7 or newer).";
            return fail(hint);
        }
        m_proc->deleteLater();
        m_proc = nullptr;
        postProcess();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && !m_cancelled)
            fail("The speech engine could not be started.");
    });
    m_proc->start(Tools::whisper(), args);
}

void TranscriptionJob::postProcess()
{
    setStage(Stage::Aligning);
    emit progress(92);
    struct Result {
        QVector<Word> words;
        QString lang;
        QString error;
        int speechRegions = 0;
    };
    auto result = std::make_shared<Result>();
    const QString jsonPath = m_jsonBase + ".json";
    const QString wav = m_wavPath;
    const bool refine = m_req.refineTiming;

    m_watcher = new QFutureWatcher<void>(this);
    connect(m_watcher, &QFutureWatcher<void>::finished, this, [this, result]() {
        m_watcher->deleteLater();
        m_watcher = nullptr;
        if (m_cancelled)
            return;
        if (!result->error.isEmpty())
            return fail(result->error);
        setStage(Stage::Segmenting);
        Transcript t;
        t.id = newId();
        t.mediaId = m_req.mediaId;
        t.language = m_req.languageCode == "auto" ? result->lang : m_req.languageCode;
        t.model = m_req.modelFile;
        t.created = QDateTime::currentDateTimeUtc();
        t.words = result->words;
        t.cues = Segmenter::segment(t.words, SegmentParams::fromStyle(m_req.styleForSegmentation));
        emit progress(100);
        setStage(Stage::Done);
        QString summary = QString("%1 words in %2 captions").arg(t.words.size()).arg(t.cues.size());
        if (t.words.isEmpty())
            summary = "No speech was recognised in this media.";
        int lowConf = 0;
        for (const Word &w : t.words)
            if (w.confidence >= 0 && w.confidence < 0.5)
                ++lowConf;
        if (lowConf)
            summary += QString(" · %1 low-confidence words highlighted for review").arg(lowConf);
        emit finished(t, summary);
    });
    m_watcher->setFuture(QtConcurrent::run([result, jsonPath, wav, refine]() {
        QFile f(jsonPath);
        if (!f.open(QIODevice::ReadOnly)) {
            result->error = "The transcription result could not be read.";
            return;
        }
        if (!WhisperParser::parseFullJson(f.readAll(), result->words, &result->lang, &result->error))
            return;
        QByteArray pcm;
        int sr = 16000;
        QString werr;
        if (readWavPcm16(wav, pcm, &sr, nullptr, &werr)) {
            const QVector<float> env = Aligner::envelopeFromPcm16(pcm, sr);
            result->speechRegions = Aligner::speechRegions(env).size();
            if (refine)
                Aligner::refine(result->words, env);
        }
    }));
}

} // namespace ss
