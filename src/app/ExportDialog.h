#pragma once

#include "../core/Document.h"
#include "../media/ExportGraph.h"

#include <QDialog>
#include <QPointer>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace ss {

class ExportJob;

// Export Center: presets, resolution, frame rate, codec, quality/bitrate, range,
// captions, destination, progress with cancel, and readable error reports.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(Document *doc, QWidget *parent = nullptr);
    void setPreset(const QString &presetId);
    void startExport();
    void reject() override;

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    ExportSettings collect() const;
    void loadSettings(const ExportSettings &s);
    void updateEstimate();
    void setRunning(bool running);

    Document *m_doc;
    QComboBox *m_preset;
    QSpinBox *m_w;
    QSpinBox *m_h;
    QComboBox *m_fps;
    QComboBox *m_codec;
    QComboBox *m_encoder;
    QComboBox *m_rateMode;
    QSlider *m_crf;
    QLabel *m_crfLabel;
    QSpinBox *m_bitrate;
    QComboBox *m_audioBitrate;
    QComboBox *m_range;
    QCheckBox *m_burn;
    QCheckBox *m_srt;
    QCheckBox *m_vtt;
    QLineEdit *m_path;
    QLabel *m_estimate;
    QProgressBar *m_progress;
    QLabel *m_status;
    QPushButton *m_export;
    QPushButton *m_cancel;
    QPushButton *m_reveal;
    QPushButton *m_log;
    QWidget *m_form;
    QPointer<ExportJob> m_job;
    QString m_lastOutput;
    QString m_lastLog;
};

} // namespace ss
