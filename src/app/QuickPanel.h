#pragma once

#include "../core/Document.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

namespace ss {

class TemplateGallery;

// Quick Editor: the six-step creator flow on the same project and engine.
// 1 Import → 2 Auto-caption → 3 Style → 4 Music → 5 Format/look → 6 Export
class QuickPanel : public QWidget {
    Q_OBJECT
public:
    explicit QuickPanel(Document *doc, QWidget *parent = nullptr);
    void setGenerationState(bool running, int percent, const QString &status);

signals:
    void importRequested();
    void generateRequested(const QString &mediaId, const QString &language);
    void addMusicRequested();
    void exportRequested(const QString &presetId);
    void editCaptionsRequested();
    void libraryRequested(const QString &category);
    void toolRequested(const QString &tool);
    void statusMessage(const QString &msg);

private:
    void refresh();
    Document *m_doc;
    QLabel *m_mediaInfo;
    QComboBox *m_language;
    QPushButton *m_generate;
    QProgressBar *m_progress;
    QLabel *m_genStatus;
    TemplateGallery *m_gallery;
    QComboBox *m_look;
    QComboBox *m_preset;
};

} // namespace ss
