#pragma once

#include "../core/Model.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QVector>

class QProcess;

namespace ss {

// Generates and caches per-project thumbnails, filmstrips and waveform peaks
// (<project>/thumbnails, <project>/waveforms) using FFmpeg in the background.
class MediaCache : public QObject {
    Q_OBJECT
public:
    static constexpr int kPeaksPerSecond = 100;

    explicit MediaCache(QObject *parent = nullptr);
    void setProjectDir(const QString &dir);
    void clearMemory();

    // Returns cached data immediately (null/empty if not ready) and schedules generation.
    QImage thumbnail(const MediaAsset &m);
    QImage filmstrip(const MediaAsset &m);
    const QVector<quint8> &peaks(const MediaAsset &m);

    QString thumbnailPath(const QString &mediaId) const;
    QString filmstripPath(const QString &mediaId) const;
    QString peaksPath(const QString &mediaId) const;

    static QVector<quint8> computePeaks(const QByteArray &pcm16Mono, int sampleRate, int peaksPerSecond);
    static bool savePeaks(const QString &path, const QVector<quint8> &peaks);
    static QVector<quint8> loadPeaks(const QString &path);

signals:
    void updated(const QString &mediaId);

private:
    struct Job {
        enum Kind { Thumb, Strip, Wave } kind;
        MediaAsset media;
    };
    void enqueue(const Job &j);
    void pumpQueue();
    void runJob(const Job &j);

    QString m_dir;
    QHash<QString, QImage> m_thumbs;
    QHash<QString, QImage> m_strips;
    QHash<QString, QVector<quint8>> m_peaks;
    QHash<QString, bool> m_pending; // key: kind+id
    QQueue<Job> m_queue;
    int m_running = 0;
    QVector<quint8> m_empty;
};

} // namespace ss
