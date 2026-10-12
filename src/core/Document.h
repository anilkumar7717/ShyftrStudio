#pragma once

#include "Model.h"

#include <QObject>
#include <QUndoStack>
#include <functional>

namespace ss {

// Owns the in-memory project, the undo stack and the selection.
// Every mutation goes through apply(): the project is snapshotted before and after,
// which keeps undo/redo exact for every kind of edit (timeline, captions, styles).
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);

    void reset(const Project &p, const QString &projectDir);
    void clear();
    bool isOpen() const { return m_open; }

    const Project &project() const { return m_project; }
    QString projectDir() const { return m_dir; }
    QString projectFile() const;

    // Applies an undoable change. If mergeKey is non-empty, consecutive changes with
    // the same key collapse into one undo step (used by sliders and drags).
    // Returns false if fn returned false (no change recorded).
    bool apply(const QString &description, const std::function<bool(Project &)> &fn, const QString &mergeKey = {});

    // Non-undoable state
    TimeUs playhead() const { return m_project.playhead; }
    void setPlayhead(TimeUs t);
    void setInOut(TimeUs in, TimeUs out);

    QStringList selectedClips() const { return m_selectedClips; }
    void setSelectedClips(const QStringList &ids);
    QString selectedCaptionTrack() const { return m_selCaptionTrack; }
    int selectedCue() const { return m_selCue; }
    void setSelectedCue(const QString &captionTrackId, int cueIndex);

    QUndoStack *undoStack() { return &m_undo; }
    bool isDirty() const { return !m_undo.isClean() || m_forcedDirty; }
    void markSaved();
    void markDirty();
    int changeCounter() const { return m_changeCounter; }

    // Restores a snapshot (used by undo commands).
    void restoreSnapshot(const QByteArray &compressedJson);
    QByteArray snapshot() const;

    // Non-undoable metadata edits (export settings, UI mode) that still need saving.
    void updateMeta(const std::function<void(Project &)> &fn);

    // For non-undoable bulk updates (relink, collect media) that must still be saved.
    void replaceProjectSilently(const Project &p);

signals:
    void projectReset();
    void projectChanged();
    void playheadChanged(ss::TimeUs t);
    void selectionChanged();
    void cueSelectionChanged();
    void dirtyChanged(bool dirty);

private:
    Project m_project;
    QString m_dir;
    bool m_open = false;
    QUndoStack m_undo;
    QStringList m_selectedClips;
    QString m_selCaptionTrack;
    int m_selCue = -1;
    bool m_forcedDirty = false;
    int m_changeCounter = 0;
    bool m_restoring = false;
};

} // namespace ss
