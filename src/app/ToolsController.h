#pragma once

#include "../core/Document.h"
#include "../media/LocalTools.h"

#include <QObject>
#include <functional>

class QWidget;

namespace ss {

class PreviewEngine;

// Runs the local "smart tools" (reverse, stabilise, freeze frame, extract audio, remove
// silences / filler words, split scenes, beat markers, text-to-speech, snapshot, markers).
// Heavy work happens on a worker thread with a cancellable progress dialog; the result is
// applied to the project as one undoable step.
class ToolsController : public QObject {
    Q_OBJECT
public:
    ToolsController(Document *doc, PreviewEngine *engine, QWidget *window);

    void reverseClip(const QString &clipId);
    void stabilizeClip(const QString &clipId);
    void freezeFrame(const QString &clipId = {});
    void extractAudio(const QString &clipId);
    void removeSilences();
    void removeFillerWords();
    void splitScenes();
    void detectBeats();
    void textToSpeech();
    void snapshot();
    void addMarker();
    void clearMarkers();
    void jumpMarker(int dir);

signals:
    void statusMessage(const QString &msg);

private:
    QString generatedDir() const;
    QString uniquePath(const QString &stem, const QString &ext) const;
    QStringList targetClips(bool needAudio, bool needVideo) const;
    // Runs fn on a worker thread with a progress dialog; fn reports progress via the callback.
    bool runWithProgress(const QString &title, const std::function<LocalTools::Result(const LocalTools::Progress &)> &fn,
                         LocalTools::Result *out);
    bool addMediaFile(const QString &path, Project &p, QString *mediaId, QString *error);
    void replaceClipMedia(const QString &clipId, const QString &newFile, const QString &what);

    Document *m_doc;
    PreviewEngine *m_engine;
    QWidget *m_window;
};

} // namespace ss
