#pragma once

#include "../core/Document.h"

#include <QScrollArea>
#include <functional>

class QFormLayout;
class QVBoxLayout;
class QGroupBox;

namespace ss {

// Context-sensitive properties: clip transform/crop/rotation/speed, fades, colour,
// audio, titles, shapes, or project canvas settings when nothing is selected.
// Edits apply to every selected clip and merge into one undo step per control.
class InspectorWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit InspectorWidget(Document *doc, QWidget *parent = nullptr);
    void focusClip(const QString &clipId);

private:
    void rebuild();
    void refreshValues();
    QGroupBox *section(const QString &title, QFormLayout **form);
    void edit(const QString &what, const QString &key, const std::function<void(Clip &, Track &)> &fn);
    void buildProject();
    void buildMedia(bool video, bool audio, bool hasSpeed);
    void buildTitle();
    void buildShape();
    void buildFades(bool audio);

    Document *m_doc;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QStringList m_builtFor;
    QString m_signature;
    QVector<std::function<void()>> m_refreshers;
    bool m_updating = false;
};

} // namespace ss
