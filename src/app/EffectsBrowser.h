#pragma once

#include "../core/Document.h"

#include <QTreeWidget>

namespace ss {

// Library of one-click transitions, titles, shapes, colour looks, speed and audio presets.
// Double-click (or Enter) applies to the selection or inserts at the playhead.
class EffectsBrowser : public QTreeWidget {
    Q_OBJECT
public:
    explicit EffectsBrowser(Document *doc, QWidget *parent = nullptr);
    // normalizedPos (0..1, canvas) places titles and shapes where they were dropped on the video.
    void applyEffect(const QString &id, const QPointF &normalizedPos = QPointF(-1, -1));
protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;

signals:
    void statusMessage(const QString &msg);

private:
    Document *m_doc;
};

} // namespace ss
