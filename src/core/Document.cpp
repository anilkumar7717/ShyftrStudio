#include "Document.h"
#include "ProjectSerializer.h"

#include <QJsonDocument>
#include <QUndoCommand>
#include <algorithm>

namespace ss {

namespace {

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Document *doc, const QString &text, QByteArray before, QByteArray after, const QString &mergeKey)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)),
          m_mergeKey(mergeKey)
    {
    }
    void undo() override { m_doc->restoreSnapshot(m_before); }
    void redo() override
    {
        if (m_first) { // already applied when pushed
            m_first = false;
            return;
        }
        m_doc->restoreSnapshot(m_after);
    }
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 0x5353; }
    bool mergeWith(const QUndoCommand *other) override
    {
        auto *o = dynamic_cast<const SnapshotCommand *>(other);
        if (!o || o->m_mergeKey != m_mergeKey || m_mergeKey.isEmpty())
            return false;
        m_after = o->m_after;
        return true;
    }

private:
    Document *m_doc;
    QByteArray m_before, m_after;
    QString m_mergeKey;
    bool m_first = true;
};

} // namespace

Document::Document(QObject *parent) : QObject(parent)
{
    m_undo.setUndoLimit(100);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool) { emit dirtyChanged(isDirty()); });
}

void Document::reset(const Project &p, const QString &projectDir)
{
    m_project = p;
    m_dir = projectDir;
    m_open = true;
    m_undo.clear();
    m_undo.setClean();
    m_forcedDirty = false;
    m_selectedClips.clear();
    m_selCaptionTrack.clear();
    m_selCue = -1;
    ++m_changeCounter;
    emit projectReset();
    emit projectChanged();
    emit selectionChanged();
    emit playheadChanged(m_project.playhead);
    emit dirtyChanged(false);
}

void Document::clear()
{
    m_project = Project();
    m_dir.clear();
    m_open = false;
    m_undo.clear();
    m_selectedClips.clear();
    emit projectReset();
}

QString Document::projectFile() const
{
    return m_dir.isEmpty() ? QString() : m_dir + "/project.json";
}

QByteArray Document::snapshot() const
{
    Project copy = m_project;
    copy.playhead = 0; // playhead is not part of undo history
    return qCompress(ProjectSerializer::toBytes(copy, true), 3);
}

void Document::restoreSnapshot(const QByteArray &compressedJson)
{
    Project p;
    QString err;
    const TimeUs playhead = m_project.playhead;
    const TimeUs in = m_project.inPoint, out = m_project.outPoint;
    if (!ProjectSerializer::fromBytes(qUncompress(compressedJson), p, &err)) {
        qWarning("Undo snapshot could not be restored: %s", qPrintable(err));
        return;
    }
    // keep runtime-only media state (missing flags) intact
    for (auto &m : p.media)
        if (const MediaAsset *old = m_project.findMedia(m.id))
            m.missing = old->missing;
    m_project = p;
    m_project.playhead = playhead;
    m_project.inPoint = in;
    m_project.outPoint = out;
    // prune selection
    QStringList sel;
    for (const QString &id : m_selectedClips)
        if (m_project.findClip(id))
            sel << id;
    m_selectedClips = sel;
    ++m_changeCounter;
    emit projectChanged();
    emit selectionChanged();
    emit dirtyChanged(isDirty());
}

bool Document::apply(const QString &description, const std::function<bool(Project &)> &fn, const QString &mergeKey)
{
    if (!m_open)
        return false;
    const QByteArray before = snapshot();
    Project working = m_project;
    if (!fn(working))
        return false;
    working.modified = QDateTime::currentDateTimeUtc();
    const QStringList problems = ProjectSerializer::validate(working);
    if (!problems.isEmpty()) {
        qWarning("Edit '%s' rejected: %s", qPrintable(description), qPrintable(problems.join("; ")));
        return false;
    }
    m_project = working;
    const QByteArray after = snapshot();
    if (after == before)
        return false;
    m_undo.push(new SnapshotCommand(this, description, before, after, mergeKey));
    ++m_changeCounter;
    QStringList sel;
    for (const QString &id : m_selectedClips)
        if (m_project.findClip(id))
            sel << id;
    const bool selChanged = sel != m_selectedClips;
    m_selectedClips = sel;
    emit projectChanged();
    if (selChanged)
        emit selectionChanged();
    emit dirtyChanged(isDirty());
    return true;
}

void Document::setPlayhead(TimeUs t)
{
    t = std::max<TimeUs>(0, t);
    if (t == m_project.playhead)
        return;
    m_project.playhead = t;
    emit playheadChanged(t);
}

void Document::setInOut(TimeUs in, TimeUs out)
{
    m_project.inPoint = in;
    m_project.outPoint = out;
    emit projectChanged();
}

void Document::setSelectedClips(const QStringList &ids)
{
    if (ids == m_selectedClips)
        return;
    m_selectedClips = ids;
    emit selectionChanged();
}

void Document::setSelectedCue(const QString &captionTrackId, int cueIndex)
{
    if (captionTrackId == m_selCaptionTrack && cueIndex == m_selCue)
        return;
    m_selCaptionTrack = captionTrackId;
    m_selCue = cueIndex;
    emit cueSelectionChanged();
}

void Document::markSaved()
{
    m_undo.setClean();
    m_forcedDirty = false;
    emit dirtyChanged(false);
}

void Document::markDirty()
{
    m_forcedDirty = true;
    emit dirtyChanged(true);
}

void Document::updateMeta(const std::function<void(Project &)> &fn)
{
    if (!m_open)
        return;
    fn(m_project);
    m_forcedDirty = true;
    emit dirtyChanged(true);
}

void Document::replaceProjectSilently(const Project &p)
{
    m_project = p;
    ++m_changeCounter;
    m_forcedDirty = true;
    // Older snapshots reference the previous media locations; undoing past this
    // point would silently break relinked media, so history restarts here.
    m_undo.clear();
    emit projectChanged();
    emit dirtyChanged(true);
}

} // namespace ss
