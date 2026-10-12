#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>

class QComboBox;
class QLabel;
class QPushButton;
class QMediaCaptureSession;
class QAudioInput;
class QMediaRecorder;

namespace ss {

// Basic voice-over recording to <project>/media/recordings.
class RecordDialog : public QDialog {
    Q_OBJECT
public:
    RecordDialog(const QString &projectDir, QWidget *parent = nullptr);
    ~RecordDialog() override;
    QString recordedFile() const { return m_file; }

private:
    void toggle();
    QString m_dir;
    QString m_file;
    QComboBox *m_device;
    QLabel *m_time;
    QLabel *m_status;
    QPushButton *m_rec;
    QMediaCaptureSession *m_session = nullptr;
    QAudioInput *m_input = nullptr;
    QMediaRecorder *m_recorder = nullptr;
    QTimer m_tick;
    QElapsedTimer m_clock;
};

} // namespace ss
