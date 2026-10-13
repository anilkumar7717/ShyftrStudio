#pragma once

#include "../captions/CaptionEngine.h"
#include "../captions/Transcription.h"
#include "../core/Document.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QCheckBox;
class QPlainTextEdit;
class QFormLayout;

namespace ss {

class TemplateGallery;

// AI Subtitles workspace: local transcription, editable transcript with synchronized
// playback, word timing, speakers, template gallery and styling, subtitle export.
class SubtitlePanel : public QWidget {
    Q_OBJECT
public:
    explicit SubtitlePanel(Document *doc, QWidget *parent = nullptr);

    void generate(const QString &mediaId, const QString &languageCode);
    bool isGenerating() const;
    void showTab(int index);
    QString currentCaptionTrack() const;
    void reloadModels();
    void importSubtitles(const QString &path);
    void exportSubtitles(const QString &format); // srt | vtt | ass | txt
    // Edits the caption under the playhead (used by the viewer's inline editor).
    bool editCaptionAt(TimeUs t, const QString &newText);
    bool captionAt(TimeUs t, QString *trackId, int *cue, QString *text) const;

signals:
    void seekRequested(ss::TimeUs t);
    void manageModelsRequested();
    void generationStateChanged(bool running, int percent, const QString &status);
    void statusMessage(const QString &msg);
    void captionsReady();

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void buildGenerateTab(QWidget *tab);
    void buildEditTab(QWidget *tab);
    void buildStyleTab(QWidget *tab);
    void refreshAll();
    void refreshSources();
    void refreshTrackCombo();
    void refreshTable();
    void refreshWords();
    void refreshStyle();
    void syncToPlayhead(TimeUs t);
    void onFinished(const Transcript &t, const QString &summary);
    void applyStyleOverride(const QString &key, const QJsonValue &v);
    void applyTemplate(const QString &id);
    bool editTranscript(const QString &what, const std::function<bool(Transcript &, Project &)> &fn,
                        const QString &mergeKey = {});
    const Track *captionTrack() const;
    const Transcript *transcript() const;
    QVector<CueInstance> instances() const;
    int selectedCue() const;
    int selectedWord() const;

    Document *m_doc;
    TranscriptionJob *m_job;
    QTabWidget *m_tabs;
    // generate
    QComboBox *m_source;
    QComboBox *m_language;
    QComboBox *m_model;
    QComboBox *m_vadModel;
    QLineEdit *m_vocab;
    QCheckBox *m_translate = nullptr;
    QCheckBox *m_refine;
    QPushButton *m_generate;
    QPushButton *m_cancel;
    QPushButton *m_retry;
    QProgressBar *m_progress;
    QLabel *m_status;
    QLabel *m_error;
    QString m_lastLog;
    QString m_pendingMediaId;
    // edit
    QComboBox *m_trackCombo;
    QLineEdit *m_search;
    QTableWidget *m_table;
    QTableWidget *m_words;
    QComboBox *m_speaker;
    QPlainTextEdit *m_cueText;
    QLabel *m_cueInfo;
    // style
    TemplateGallery *m_gallery;
    QWidget *m_styleForm;
    QVector<std::function<void()>> m_styleRefreshers;
    bool m_updating = false;
    QString m_activeTrack;
};

} // namespace ss
