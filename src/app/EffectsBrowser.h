#pragma once

#include "../core/Document.h"

#include <QListWidget>
#include <QWidget>

class QLineEdit;
class QTabWidget;

namespace ss {

// A grid of library items that can be dragged onto the video or the timeline.
class LibraryList : public QListWidget {
    Q_OBJECT
public:
    explicit LibraryList(QWidget *parent = nullptr);
    void setBaseThumb(QSize s);

protected:
    void resizeEvent(QResizeEvent *e) override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;

private:
    void fitGrid();
    QSize m_base{88, 50};
};

// The creative library (like a mobile editor's bottom bar): Text templates, Stickers,
// Effects, Transitions, Filters, Shapes, Speed curves, Voice effects and quick Basics.
// Every item has a live thumbnail rendered with the same code as the export.
// Double-click applies to the selection (or inserts at the playhead); drag onto the video
// to place text/stickers/shapes, or onto a clip / the timeline to apply effects.
class EffectsBrowser : public QWidget {
    Q_OBJECT
public:
    explicit EffectsBrowser(Document *doc, QWidget *parent = nullptr);
    // pos: drop position on the video (0..1); targetClip: clip it was dropped on ("-" = dropped
    // on empty timeline space, so not the selection); at: drop time; trackId: timeline track it
    // was dropped on (TimelineOps::kNewTrack for the "new track" lane).
    void applyEffect(const QString &id, const QPointF &normalizedPos = QPointF(-1, -1), const QString &targetClip = {},
                     TimeUs at = -1, const QString &trackId = {});
    // What dropping this item does: "clip" = creates a clip; "target" = needs a clip under it;
    // "either" = applies to a clip under it, or creates an effect layer on empty space.
    static QString dropKind(const QString &id);
    // Length of the clip a drop of this item creates.
    static TimeUs dropDuration(const QString &id);
    void showCategory(const QString &name);

signals:
    void statusMessage(const QString &msg);

private:
    void build();
    void filter(const QString &text);
    LibraryList *addPage(const QString &title, int thumbW, int thumbH);

    Document *m_doc;
    QLineEdit *m_search = nullptr;
    QTabWidget *m_tabs = nullptr;
    QVector<LibraryList *> m_lists;
    QString m_targetTrack; // track for clips created by the current applyEffect call
};

namespace Library {
struct TextTemplate {
    QString id;
    QString label;
    TitleProps props;
};
QVector<TextTemplate> textTemplates();
QStringList stickers();
} // namespace Library

} // namespace ss
