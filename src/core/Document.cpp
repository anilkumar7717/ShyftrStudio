#include "Document.h"
#include "ProjectSerializer.h"

#include <QJsonDocument>
#include <QUndoCommand>
#include <algorithm>

namespace ss {

namespace {

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Document *doc, const QString &text, Project before, Project after, const QString &mergeKey)
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
    Project m_before, m_after; // implicitly shared: only what an edit changed takes memory
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

void Document::restoreSnapshot(const Project &snap)
{
    Project p = snap;
    // keep runtime/view state that is not part of the undo history
    for (int i = 0; i < p.media.size(); ++i)
        if (const MediaAsset *old = m_project.findMedia(p.media[i].id); old && old->missing != p.media[i].missing)
            p.media[i].missing = old->missing;
    p.playhead = m_project.playhead;
    p.inPoint = m_project.inPoint;
    p.outPoint = m_project.outPoint;
    m_project = p;
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
    const Project before = m_project; // shares all data until the edit changes something
    Project working = m_project;
    if (!fn(working))
        return false;
    working.modified = QDateTime::currentDateTimeUtc();
    const QStringList problems = ProjectSerializer::validate(working);
    if (!problems.isEmpty()) {
        qWarning("Edit '%s' rejected: %s", qPrintable(description), qPrintable(problems.join("; ")));
        return false;
    }
    if (ProjectSerializer::sameContent(before, working))
        return false;
    m_project = working;
    m_undo.push(new SnapshotCommand(this, description, before, m_project, mergeKey));
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
