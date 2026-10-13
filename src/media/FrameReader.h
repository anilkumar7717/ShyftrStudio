#pragma once

#include "Compositor.h"

#include <QHash>
#include <QImage>
#include <QSize>

class QProcess;

namespace ss {

// Frame-accurate decoding for export: one FFmpeg process per visible clip streams raw RGBA
// frames at a constant rate from the clip's first needed source time, so frames are read
// strictly forwards (no seeking per frame). Still images are loaded once.
class FrameReader : public FrameSource {
public:
    FrameReader(const Project &p, QSize outputSize);
    ~FrameReader() override;
    QImage frame(const Clip &clip, const MediaAsset &media, TimeUs sourceTime) override;
    // Stops decoders for clips that are no longer needed.
    void retain(const QSet<QString> &clipIds);
    QString lastError() const { return m_error; }

private:
    struct Decoder {
        QProcess *proc = nullptr;
        TimeUs startSrc = 0;
        double rate = 30;      // decoded frames per source second
        qint64 index = -1;     // index of `last`
        QImage last;
        QSize size;
        bool eof = false;
    };
    Decoder *open(const Clip &clip, const MediaAsset &media, TimeUs startSrc);
    void close(Decoder *d);
    QSize decodeSize(const Clip &clip, const MediaAsset &media) const;

    const Project &m_project;
    QSize m_out;
    QHash<QString, Decoder *> m_decoders; // by clip id
    QHash<QString, QImage> m_stills;      // by media id
    QString m_error;
};

} // namespace ss
