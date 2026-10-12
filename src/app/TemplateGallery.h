#pragma once

#include <QListWidget>

namespace ss {

// Grid of caption templates rendered live with the real caption renderer.
class TemplateGallery : public QListWidget {
    Q_OBJECT
public:
    explicit TemplateGallery(QWidget *parent = nullptr);
    void setAspect(int w, int h);
    void setCurrentTemplate(const QString &id);
    void reload();
    void setCompact(bool compact);
signals:
    void templateChosen(const QString &id);

private:
    QSize m_aspect{16, 9};
    bool m_compact = false;
    QString m_current;
};

} // namespace ss
