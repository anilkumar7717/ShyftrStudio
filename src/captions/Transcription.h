#pragma once

#include "../core/Model.h"
#include "CaptionStyle.h"

#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QProcess>

namespace ss {

// ------------------------------------------------------------------ languages
struct LanguageOption {
    QString code;         // stored in the transcript
    QString label;
    QString whisperLang;  // value passed to whisper.cpp -l
    QString prompt;       // initial prompt that nudges script / style
    QString note;
};

namespace Languages {
QVector<LanguageOption> all();
LanguageOption byCode(const QString &code);
} // namespace Languages

// ------------------------------------------------------------------ models
struct ModelInfo {
    QString file;        // file name inside AI_Models
    QString path;
    QString name;        // e.g. "ggml-small"
    qint64 size = 0;
    QString sha256;      // recorded at import / first verification
    QDateTime mtime;
    QDateTime addedAt;
    QString provenance;  // where the file came from (user supplied)
    QString license;
    bool isVad = false;
    bool bundled = false; // shipped read-only next to the app
    QString status;      // "verified", "unverified", "changed", "missing"
};

class ModelManager {
public:
    static ModelManager &instance();
    QString dir() const;
    QVector<ModelInfo> scan();                 // reads manifest + folder
    QVector<ModelInfo> speechModels();
    QVector<ModelInfo> vadModels();
    ModelInfo find(const QString &file);

    // Copies a model file into AI_Models with a disk-space check, computes SHA-256 and
    // records provenance. expectedSha256 (optional) must match or the import fails.
    bool importModel(const QString &sourcePath, const QString &provenance, const QString &expectedSha256,
                     QString *error, const std::function<bool(qint64, qint64)> &progress = {});
    // Recomputes SHA-256 and compares with the manifest (records it on first use).
    bool verify(const QString &file, QString *error, const std::function<bool(qint64, qint64)> &progress = {});
    // Fast check used before transcription: re-hash only if size/mtime changed.
    bool quickVerify(const QString &file, QString *error);
    bool remove(const QString &file, QString *error);
    // Checks the file really is a whisper.cpp model (ggml "lmgg" or GGUF header), not a
    // web page or truncated download. Returns false with a user-facing explanation.
    static bool checkModelFile(const QString &path, QString *error);
    static QString sha256Of(const QString &path, const std::function<bool(qint64, qint64)> &progress = {});

    struct CatalogEntry {
        QString name;
        qint64 approxBytes;
        QString ram;
        QString note;
    };
    static QVector<CatalogEntry> catalog();
    static QString describeSize(qint64 bytes);
    // Read-only model folders shipped with the app (e.g. <app>/models on Windows).
    static QStringList bundledDirs();

private:
    bool saveManifest(const QVector<ModelInfo> &models) const;
    QVector<ModelInfo> loadManifest() const;
};

// ------------------------------------------------------------------ job
struct TranscriptionRequest {
    QString mediaId;
    QString mediaPath;
    TimeUs mediaDuration = 0;
    QString languageCode = "auto";
    QString modelFile;
    QString vadModelFile;     // optional
    QString extraPrompt;      // custom vocabulary / names
    int threads = 0;
    bool useGpu = true;
    bool refineTiming = true;
    bool translateToEnglish = false; // whisper's built-in translation (any language -> English)
    QString workDir;
    CaptionStyle styleForSegmentation;
};

class TranscriptionJob : public QObject {
    Q_OBJECT
public:
    enum class Stage { Idle, Extracting, DetectingSpeech, Transcribing, Aligning, Segmenting, Done, Failed, Cancelled };
    Q_ENUM(Stage)

    explicit TranscriptionJob(QObject *parent = nullptr);
    ~TranscriptionJob() override;
    void start(const TranscriptionRequest &req);
    void cancel();
    bool isRunning() const;
    Stage stage() const { return m_stage; }
    static QString stageLabel(Stage s);
    QString log() const { return m_log; }

signals:
    void stageChanged(ss::TranscriptionJob::Stage stage, const QString &label);
    void progress(int percent);
    void finished(const ss::Transcript &transcript, const QString &summary);
    void failed(const QString &message, const QString &details);
    void cancelled();

private:
    void setStage(Stage s);
    void fail(const QString &msg);
    void runExtract();
    void runWhisper();
    void postProcess();
    void cleanupProcess();

    TranscriptionRequest m_req;
    Stage m_stage = Stage::Idle;
    QPointer<QProcess> m_proc;
    QString m_log;
    QString m_wavPath;
    QString m_jsonBase;
    QByteArray m_stderrBuf;
    bool m_cancelled = false;
    QFutureWatcher<void> *m_watcher = nullptr;
};

// Reads PCM s16 data from a canonical or extended WAV file.
bool readWavPcm16(const QString &path, QByteArray &pcm, int *sampleRate, int *channels, QString *error);

} // namespace ss

Q_DECLARE_METATYPE(ss::Transcript)
