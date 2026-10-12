#pragma once

#include "../captions/OverlayRenderer.h"
#include "../core/Document.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>

class QMediaPlayer;
class QVideoSink;
class QAudioOutput;
class QVideoFrame;

namespace ss {

// Plays the multi-layer timeline: one decoder per visible video clip (pooled), separate
// audio decoders for audio clips, synchronised to a master clock. Frames are composited
// with the same geometry/colour maths as export, and the same OverlayScene draws titles
// and captions on top.
class PreviewEngine : public QObject {
    Q_OBJECT
public:
    explicit PreviewEngine(Document *doc, QObject *parent = nullptr);
    ~PreviewEngine() override;

    void setMaxPreviewWidth(int w);
    QSize outputSize() const;

    bool isPlaying() const { return m_playing; }
    double rate() const { return m_rate; }
    void play();
    void pause();
    void togglePlay();
    void shuttle(int direction); // J (-1) / K (0) / L (+1)
    void seek(TimeUs t);
    void stepFrames(int n);
    void setLoopInOut(bool on) { m_loop = on; }
    void setShowSafeArea(bool on);
    bool showSafeArea() const { return m_safeArea; }
    QImage currentFrame() const { return m_frame; }
    void invalidate(); // project changed: rebuild overlay and recompose

signals:
    void frameReady(const QImage &frame);
    void playStateChanged(bool playing);
    void statusMessage(const QString &msg);

private:
    struct Slot {
        QString key;      // clip id
        QString mediaId;
        QMediaPlayer *player = nullptr;
        QVideoSink *sink = nullptr;
        QAudioOutput *audio = nullptr;
        QImage frame;
        bool isAudio = false;
        bool ready = false;
        qint64 lastUse = 0;
    };

    void tick();
    void syncSlots(TimeUs t, bool playing);
    Slot *slotFor(const Clip &c, bool audio);
    void releaseUnused(const QSet<QString> &keep);
    void compose(TimeUs t);
    void scheduleCompose();
    QImage stillImage(const MediaAsset &m);
    void drawClip(QPainter &p, const Clip &c, const MediaAsset &m, const QImage &src, TimeUs t);

    Document *m_doc;
    QHash<QString, Slot *> m_slots;
    QHash<QString, QImage> m_stills;
    OverlayScene m_overlay;
    QTimer m_timer;
    QTimer m_composeTimer;
    QElapsedTimer m_clock;
    TimeUs m_clockBase = 0;
    bool m_playing = false;
    double m_rate = 1.0;
    bool m_loop = false;
    bool m_safeArea = false;
    int m_maxWidth = 960;
    QImage m_frame;
    qint64 m_useCounter = 0;
    TimeUs m_lastComposeT = -1;
};

} // namespace ss
