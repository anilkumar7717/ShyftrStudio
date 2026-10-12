#include "RecordDialog.h"
#include "../core/Model.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ss {

RecordDialog::RecordDialog(const QString &projectDir, QWidget *parent) : QDialog(parent), m_dir(projectDir + "/media/recordings")
{
    setWindowTitle("Record voice-over");
    auto *v = new QVBoxLayout(this);
    auto *f = new QFormLayout;
    m_device = new QComboBox;
    for (const QAudioDevice &d : QMediaDevices::audioInputs())
        m_device->addItem(d.description(), QVariant::fromValue(d));
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    for (int i = 0; i < m_device->count(); ++i)
        if (m_device->itemData(i).value<QAudioDevice>() == def)
            m_device->setCurrentIndex(i);
    f->addRow("Microphone", m_device);
    v->addLayout(f);
    m_time = new QLabel("00:00:00");
    m_time->setAlignment(Qt::AlignCenter);
    m_time->setStyleSheet("font-size: 28pt; font-family: Menlo, Consolas, monospace;");
    v->addWidget(m_time);
    m_status = new QLabel(m_device->count() ? "Ready. Recording stays in this project's media folder."
                                            : "No microphone found. Check your system privacy settings.");
    m_status->setWordWrap(true);
    m_status->setObjectName("hint");
    v->addWidget(m_status);
    auto *h = new QHBoxLayout;
    m_rec = new QPushButton("● Record");
    m_rec->setObjectName("primary");
    m_rec->setEnabled(m_device->count() > 0);
    auto *done = new QPushButton("Done");
    h->addStretch();
    h->addWidget(m_rec);
    h->addWidget(done);
    v->addLayout(h);

    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() { m_time->setText(formatClock(m_clock.elapsed() * kUsPerMs, false)); });
    connect(m_rec, &QPushButton::clicked, this, &RecordDialog::toggle);
    connect(done, &QPushButton::clicked, this, [this]() {
        if (m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState)
            toggle();
        accept();
    });
}

RecordDialog::~RecordDialog()
{
    if (m_recorder)
        m_recorder->stop();
}

void RecordDialog::toggle()
{
    if (m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState) {
        m_recorder->stop();
        m_tick.stop();
        m_rec->setText("● Record again");
        m_status->setText("Saved " + QDir::toNativeSeparators(m_file) + ". Click Done to add it to the project.");
        return;
    }
    QDir().mkpath(m_dir);
    delete m_recorder;
    delete m_input;
    delete m_session;
    m_session = new QMediaCaptureSession(this);
    m_input = new QAudioInput(m_device->currentData().value<QAudioDevice>(), this);
    m_recorder = new QMediaRecorder(this);
    m_session->setAudioInput(m_input);
    m_session->setRecorder(m_recorder);
    QMediaFormat fmt;
    fmt.setFileFormat(QMediaFormat::Mpeg4Audio);
    fmt.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    m_recorder->setMediaFormat(fmt);
    m_recorder->setQuality(QMediaRecorder::HighQuality);
    m_file = m_dir + "/voiceover-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".m4a";
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_file));
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &msg) {
        m_status->setText("Recording error: " + msg);
        m_tick.stop();
        m_rec->setText("● Record");
    });
    connect(m_recorder, &QMediaRecorder::actualLocationChanged, this, [this](const QUrl &u) {
        if (u.isLocalFile())
            m_file = u.toLocalFile();
    });
    m_recorder->record();
    m_clock.start();
    m_tick.start();
    m_rec->setText("■ Stop");
    m_status->setText("Recording…");
}

} // namespace ss
