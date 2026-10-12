#include "MediaCache.h"
#include "../core/Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdlib>
#include <algorithm>

namespace ss {

MediaCache::MediaCache(QObject *parent) : QObject(parent) {}

void MediaCache::setProjectDir(const QString &dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    clearMemory();
}

void MediaCache::clearMemory()
{
    m_thumbs.clear();
    m_strips.clear();
    m_peaks.clear();
    m_pending.clear();
    m_queue.clear();
}

QString MediaCache::thumbnailPath(const QString &id) const { return m_dir + "/thumbnails/" + id + ".jpg"; }
QString MediaCache::filmstripPath(const QString &id) const { return m_dir + "/thumbnails/" + id + "-strip.jpg"; }
QString MediaCache::peaksPath(const QString &id) const { return m_dir + "/waveforms/" + id + ".peaks"; }

QImage MediaCache::thumbnail(const MediaAsset &m)
{
    if (m_thumbs.contains(m.id))
        return m_thumbs.value(m.id);
    if (m.kind == MediaKind::Image && !m.missing) {
        QImageReader r(m.path);
        r.setAutoTransform(true);
        r.setScaledSize(r.size().scaled(320, 180, Qt::KeepAspectRatio));
        QImage img = r.read();
        m_thumbs.insert(m.id, img);
        return img;
    }
    const QString p = thumbnailPath(m.id);
    if (QFileInfo::exists(p)) {
        QImage img(p);
        m_thumbs.insert(m.id, img);
        return img;
    }
    if (m.kind == MediaKind::Video && !m.missing)
        enqueue({Job::Thumb, m});
    return {};
}

QImage MediaCache::filmstrip(const MediaAsset &m)
{
    if (m_strips.contains(m.id))
        return m_strips.value(m.id);
    if (m.kind == MediaKind::Image)
        return thumbnail(m);
    const QString p = filmstripPath(m.id);
    if (QFileInfo::exists(p)) {
        QImage img(p);
        m_strips.insert(m.id, img);
        return img;
    }
    if (m.kind == MediaKind::Video && !m.missing)
        enqueue({Job::Strip, m});
    return {};
}

const QVector<quint8> &MediaCache::peaks(const MediaAsset &m)
{
    auto it = m_peaks.find(m.id);
    if (it != m_peaks.end())
        return it.value();
    const QString p = peaksPath(m.id);
    if (QFileInfo::exists(p)) {
        m_peaks.insert(m.id, loadPeaks(p));
        return m_peaks[m.id];
    }
    if (m.hasAudio && !m.missing)
        enqueue({Job::Wave, m});
    return m_empty;
}

void MediaCache::enqueue(const Job &j)
{
    if (m_dir.isEmpty())
        return;
    const QString key = QString::number(j.kind) + j.media.id;
    if (m_pending.contains(key))
        return;
    m_pending.insert(key, true);
    m_queue.enqueue(j);
    pumpQueue();
}

void MediaCache::pumpQueue()
{
    while (m_running < 2 && !m_queue.isEmpty())
        runJob(m_queue.dequeue());
}

void MediaCache::runJob(const Job &j)
{
    const QString ffmpeg = Tools::ffmpeg();
    if (ffmpeg.isEmpty())
        return;
    QDir().mkpath(m_dir + "/thumbnails");
    QDir().mkpath(m_dir + "/waveforms");
    ++m_running;
    auto *proc = new QProcess(this);
    const MediaAsset m = j.media;
    const double dur = std::max(0.1, m.duration / 1e6);
    QStringList args{"-hide_banner", "-nostdin", "-loglevel", "error", "-y"};
    QString target;
    if (j.kind == Job::Thumb) {
        target = thumbnailPath(m.id);
        args << "-ss" << QString::number(std::min(1.0, dur / 3), 'f', 3) << "-i" << m.path << "-frames:v" << "1" << "-vf"
             << "scale=320:-2" << "-q:v" << "4" << target + ".tmp.jpg";
    } else if (j.kind == Job::Strip) {
        target = filmstripPath(m.id);
        const int tiles = 12;
        args << "-i" << m.path << "-vf"
             << QString("fps=%1,scale=-2:72,tile=%2x1").arg(QString::number(tiles / dur, 'f', 6)).arg(tiles)
             << "-frames:v" << "1" << "-q:v" << "5" << target + ".tmp.jpg";
    } else {
        target = peaksPath(m.id);
        args << "-i" << m.path << "-vn" << "-ac" << "1" << "-ar" << "8000" << "-f" << "s16le" << "-";
    }
    auto pcm = std::make_shared<QByteArray>();
    if (j.kind == Job::Wave)
        connect(proc, &QProcess::readyReadStandardOutput, proc, [proc, pcm]() { pcm->append(proc->readAllStandardOutput()); });
    connect(proc, &QProcess::finished, this, [this, proc, j, target, pcm](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        --m_running;
        const QString id = j.media.id;
        if (st == QProcess::NormalExit && code == 0) {
            if (j.kind == Job::Wave) {
                pcm->append(proc->readAllStandardOutput());
                const QVector<quint8> pk = computePeaks(*pcm, 8000, kPeaksPerSecond);
                savePeaks(target, pk);
                m_peaks.insert(id, pk);
            } else {
                QFile::remove(target);
                QFile::rename(target + ".tmp.jpg", target);
                QImage img(target);
                if (j.kind == Job::Thumb)
                    m_thumbs.insert(id, img);
                else
                    m_strips.insert(id, img);
            }
            emit updated(id);
        } else {
            QFile::remove(target + ".tmp.jpg");
        }
        pumpQueue();
    });
    proc->start(ffmpeg, args);
}

QVector<quint8> MediaCache::computePeaks(const QByteArray &pcm, int sampleRate, int pps)
{
    QVector<quint8> out;
    const auto *s = reinterpret_cast<const qint16 *>(pcm.constData());
    const int n = pcm.size() / 2;
    const int win = std::max(1, sampleRate / pps);
    out.reserve(n / win + 1);
    for (int i = 0; i < n; i += win) {
        int peak = 0;
        const int end = std::min(n, i + win);
        for (int k = i; k < end; ++k)
            peak = std::max(peak, std::abs(int(s[k])));
        // perceptual-ish scaling: sqrt to make quiet speech visible
        out << quint8(std::clamp(int(std::sqrt(peak / 32768.0) * 255.0), 0, 255));
    }
    return out;
}

bool MediaCache::savePeaks(const QString &path, const QVector<quint8> &peaks)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("SSPK1");
    f.write(reinterpret_cast<const char *>(peaks.constData()), peaks.size());
    return f.commit();
}

QVector<quint8> MediaCache::loadPeaks(const QString &path)
{
    QFile f(path);
    QVector<quint8> out;
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray d = f.readAll();
    if (!d.startsWith("SSPK1"))
        return out;
    out.resize(d.size() - 5);
    memcpy(out.data(), d.constData() + 5, out.size());
    return out;
}

} // namespace ss
