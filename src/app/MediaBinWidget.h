#pragma once

#include "../core/Document.h"

#include <QListWidget>
#include <QWidget>

class QLineEdit;

namespace ss {

class MediaCache;

class MediaListWidget : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;
signals:
    void filesDropped(const QStringList &paths);

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

// Project media bin: thumbnails, metadata, drag to timeline.
class MediaBinWidget : public QWidget {
    Q_OBJECT
public:
    MediaBinWidget(Document *doc, MediaCache *cache, QWidget *parent = nullptr);
    QStringList selectedMediaIds() const;

signals:
    void importRequested();
    void recordRequested();
    void relinkRequested();
    void filesDropped(const QStringList &paths);
    void addToTimelineRequested(const QStringList &mediaIds);

private:
    void refresh();
    void refreshIcons(const QString &mediaId);
    void showMenu(const QPoint &pos);
    Document *m_doc;
    MediaCache *m_cache;
    MediaListWidget *m_list;
    QLineEdit *m_filter;
};

} // namespace ss
