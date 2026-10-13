#include "ExportDialog.h"
#include "../captions/SubtitleIO.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "../media/ExportJob.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include "Widgets.h"
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <limits>
#include <algorithm>

namespace ss {

ExportDialog::ExportDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle("Export Center");
    setMinimumWidth(420);
    auto *v = new QVBoxLayout(this);
    m_form = new QWidget;
    auto *f = new QFormLayout(m_form);
    f->setLabelAlignment(Qt::AlignRight);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *formScroll = new QScrollArea;
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    formScroll->setWidget(m_form);
    v->addWidget(formScroll, 1);
    if (QScreen *scr = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
        const QRect a = scr->availableGeometry();
        resize(std::min(640, int(a.width() * 0.9)), std::min(760, int(a.height() * 0.88)));
    }

    m_preset = new QComboBox;
    m_preset->addItem("Match project canvas", "project");
    for (const ExportPreset &p : exportPresets())
        m_preset->addItem(p.label, p.id);
    f->addRow("Preset", m_preset);

    auto *size = new QHBoxLayout;
    m_w = new QSpinBox;
    m_w->setRange(128, 7680);
    m_w->setSingleStep(2);
    m_h = new QSpinBox;
    m_h->setRange(128, 4320);
    m_h->setSingleStep(2);
    size->addWidget(m_w);
    size->addWidget(new QLabel("×"));
    size->addWidget(m_h);
    size->addStretch();
    f->addRow("Resolution", size);
    m_fps = new QComboBox;
    for (const auto &[l, r] : QVector<QPair<QString, QString>>{{"23.976", "24000/1001"}, {"24", "24/1"}, {"25", "25/1"},
                                                                {"29.97", "30000/1001"}, {"30", "30/1"}, {"50", "50/1"},
                                                                {"59.94", "60000/1001"}, {"60", "60/1"}})
        m_fps->addItem(l + " fps", r);
    f->addRow("Frame rate", m_fps);
    m_codec = new QComboBox;
    m_codec->addItem("H.264 (MP4, most compatible)", "h264");
    m_codec->addItem("H.265 / HEVC (smaller files)", "hevc");
    f->addRow("Codec", m_codec);
    m_encoder = new QComboBox;
    m_encoder->addItem("Automatic (hardware when available)", "auto");
    m_encoder->addItem("Hardware", "hardware");
    m_encoder->addItem("Software (best quality per bit)", "software");
    f->addRow("Encoder", m_encoder);
    m_rateMode = new QComboBox;
    m_rateMode->addItem("Constant quality", "crf");
    m_rateMode->addItem("Target bitrate", "bitrate");
    f->addRow("Rate control", m_rateMode);
    auto *q = new QHBoxLayout;
    m_crf = new QSlider(Qt::Horizontal);
    m_crf->setRange(14, 32);
    m_crf->setInvertedAppearance(true);
    m_crfLabel = new QLabel;
    q->addWidget(new QLabel("Smaller"));
    q->addWidget(m_crf, 1);
    q->addWidget(new QLabel("Better"));
    q->addWidget(m_crfLabel);
    f->addRow("Quality", q);
    m_bitrate = new QSpinBox;
    m_bitrate->setRange(500, 200000);
    m_bitrate->setSingleStep(500);
    m_bitrate->setSuffix(" kbps");
    f->addRow("Video bitrate", m_bitrate);
    m_audioBitrate = new QComboBox;
    for (int k : {128, 160, 192, 256, 320})
        m_audioBitrate->addItem(QString("%1 kbps AAC").arg(k), k);
    f->addRow("Audio", m_audioBitrate);
    m_range = new QComboBox;
    m_range->addItem("Whole timeline", "all");
    m_range->addItem("In → Out range", "inout");
    m_range->addItem("Selected clips", "selection");
    f->addRow("Range", m_range);
    m_burn = new QCheckBox("Burn captions into the video");
    m_srt = new QCheckBox("Also save .srt");
    m_vtt = new QCheckBox("Also save .vtt");
    f->addRow("Captions", flowRow({m_burn, m_srt, m_vtt}));
    m_loud = new QCheckBox("Normalise loudness (−14 LUFS, like YouTube/Instagram)");
    f->addRow("Sound", m_loud);
    m_format = new QComboBox;
    m_format->addItem("MP4 video", "mp4");
    m_format->addItem("MOV video", "mov");
    m_format->addItem("GIF animation", "gif");
    m_format->addItem("MP3 audio only", "mp3");
    m_format->addItem("WAV audio only", "wav");
    m_format->addItem("M4A audio only", "m4a");
    f->addRow("Format", m_format);
    auto *dest = new QHBoxLayout;
    m_path = new QLineEdit;
    auto *browse = new QPushButton("Choose…");
    dest->addWidget(m_path, 1);
    dest->addWidget(browse);
    f->addRow("Save to", dest);
    m_estimate = new QLabel;
    m_estimate->setObjectName("hint");
    f->addRow("", m_estimate);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    v->addWidget(m_progress);
    m_status = new QLabel("Rendering runs entirely on this computer.");
    m_status->setObjectName("hint");
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_status);

    auto *btns = new QHBoxLayout;
    btns->setSpacing(6);
    m_reveal = new QPushButton("Show in folder");
    m_reveal->hide();
    m_log = new QPushButton("Open error log");
    m_log->hide();
    m_cancel = new QPushButton("Cancel export");
    m_cancel->hide();
    m_export = new QPushButton("Export");
    m_export->setObjectName("primary");
    m_export->setDefault(true);
    auto *close = new QPushButton("Close");
    btns->addWidget(m_reveal);
    btns->addWidget(m_log);
    btns->addStretch();
    btns->addWidget(m_cancel);
    btns->addWidget(close);
    btns->addWidget(m_export);
    v->addLayout(btns);

    // defaults: last used settings, policy defaults, else project canvas
    ExportSettings s = ExportSettings::fromJson(doc->project().exportSettings);
    const Policy pol = Policy::load();
    if (doc->project().exportSettings.isEmpty() && !pol.defaultExport.isEmpty())
        s = ExportSettings::fromJson(pol.defaultExport);
    if (doc->project().exportSettings.isEmpty()) {
        s.presetId = "project";
        s.width = doc->project().canvas.width;
        s.height = doc->project().canvas.height;
        s.fps = doc->project().canvas.fps;
    }
    QString dir = doc->project().exportSettings.value("lastOutputDir").toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty())
        dir = doc->projectDir() + "/exports";
    s.outputPath = dir + "/" + doc->project().name + ".mp4";
    loadSettings(s);

    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int) { setPreset(m_preset->currentData().toString()); });
    connect(m_rateMode, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool crf = m_rateMode->currentData().toString() == "crf";
        m_crf->setEnabled(crf);
        m_bitrate->setEnabled(!crf);
        updateEstimate();
    });
    for (QSpinBox *sb : {m_w, m_h, m_bitrate})
        connect(sb, &QSpinBox::valueChanged, this, &ExportDialog::updateEstimate);
    connect(m_crf, &QSlider::valueChanged, this, &ExportDialog::updateEstimate);
    connect(m_range, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimate);
    connect(m_codec, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimate);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getSaveFileName(this, "Export", m_path->text(),
                                                       "MP4 video (*.mp4);;QuickTime (*.mov);;GIF animation (*.gif);;MP3 audio (*.mp3);;"
                                                       "WAV audio (*.wav);;M4A audio (*.m4a)");
        if (!p.isEmpty()) {
            m_path->setText(p);
            const int i = m_format->findData(QFileInfo(p).suffix().toLower());
            if (i >= 0)
                m_format->setCurrentIndex(i);
        }
    });
    connect(m_format, &QComboBox::currentIndexChanged, this, [this](int) {
        // keep the file name, change its extension
        const QString ext = m_format->currentData().toString();
        QString path = m_path->text().trimmed();
        if (!path.isEmpty()) {
            const QFileInfo fi(path);
            m_path->setText(fi.absolutePath() + "/" + fi.completeBaseName() + "." + ext);
        }
        const bool audioOnly = ext == "mp3" || ext == "wav" || ext == "m4a";
        for (QWidget *w : QList<QWidget *>{m_burn, m_codec, m_encoder, m_rateMode, m_crf, m_bitrate})
            w->setEnabled(!audioOnly);
    });
    connect(m_export, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (m_job)
            m_job->cancel();
        m_status->setText("Cancelling…");
    });
    connect(close, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(m_reveal, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastOutput).absolutePath()));
    });
    connect(m_log, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastLog)); });
    updateEstimate();
}

void ExportDialog::loadSettings(const ExportSettings &s)
{
    const int pi = m_preset->findData(s.presetId);
    m_preset->blockSignals(true);
    m_preset->setCurrentIndex(pi >= 0 ? pi : 0);
    m_preset->blockSignals(false);
    m_w->setValue(s.width);
    m_h->setValue(s.height);
    const int fi = m_fps->findData(s.fps.toString());
    m_fps->setCurrentIndex(fi >= 0 ? fi : m_fps->findData("30/1"));
    m_codec->setCurrentIndex(std::max(0, m_codec->findData(s.videoCodec)));
    m_encoder->setCurrentIndex(std::max(0, m_encoder->findData(
                                               Settings::instance().encoderPreference == "auto" ? s.encoder : Settings::instance().encoderPreference)));
    m_rateMode->setCurrentIndex(std::max(0, m_rateMode->findData(s.rateMode)));
    m_crf->setValue(s.crf);
    m_bitrate->setValue(s.videoBitrateKbps);
    m_bitrate->setEnabled(s.rateMode == "bitrate");
    m_crf->setEnabled(s.rateMode == "crf");
    m_audioBitrate->setCurrentIndex(std::max(0, m_audioBitrate->findData(s.audioBitrateKbps)));
    m_burn->setChecked(s.burnCaptions);
    m_srt->setChecked(s.writeSrt);
    m_vtt->setChecked(s.writeVtt);
    m_path->setText(s.outputPath);
    m_loud->setChecked(s.normalizeLoudness);
    {
        const int fi = m_format->findData(QFileInfo(s.outputPath).suffix().toLower());
        m_format->blockSignals(true);
        m_format->setCurrentIndex(std::max(0, fi));
        m_format->blockSignals(false);
    }
    if (m_doc->project().inPoint >= 0 && m_doc->project().outPoint > m_doc->project().inPoint)
        m_range->setCurrentIndex(1);
}

void ExportDialog::setPreset(const QString &id)
{
    const int idx = m_preset->findData(id);
    if (idx >= 0 && m_preset->currentIndex() != idx) {
        m_preset->setCurrentIndex(idx); // re-enters via signal
        return;
    }
    if (id == "project") {
        m_w->setValue(m_doc->project().canvas.width);
        m_h->setValue(m_doc->project().canvas.height);
        const int fi = m_fps->findData(m_doc->project().canvas.fps.toString());
        if (fi >= 0)
            m_fps->setCurrentIndex(fi);
    } else {
        for (const ExportPreset &p : exportPresets())
            if (p.id == id) {
                m_w->setValue(p.width);
                m_h->setValue(p.height);
                m_codec->setCurrentIndex(std::max(0, m_codec->findData(p.codec)));
                m_crf->setValue(p.crf);
                m_bitrate->setValue(p.bitrateKbps);
                const Rational pf{p.fpsNum, p.fpsDen};
                // keep the project frame rate when it is a standard one
                const int fi = m_fps->findData(m_doc->project().canvas.fps.toString());
                m_fps->setCurrentIndex(fi >= 0 ? fi : m_fps->findData(pf.toString()));
            }
    }
    updateEstimate();
}

ExportSettings ExportDialog::collect() const
{
    ExportSettings s;
    s.presetId = m_preset->currentData().toString();
    s.width = m_w->value() & ~1;
    s.height = m_h->value() & ~1;
    s.fps = Rational::fromString(m_fps->currentData().toString());
    s.videoCodec = m_codec->currentData().toString();
    s.encoder = m_encoder->currentData().toString();
    s.rateMode = m_rateMode->currentData().toString();
    s.crf = m_crf->value();
    s.videoBitrateKbps = m_bitrate->value();
    s.audioBitrateKbps = m_audioBitrate->currentData().toInt();
    s.burnCaptions = m_burn->isChecked();
    s.writeSrt = m_srt->isChecked();
    s.writeVtt = m_vtt->isChecked();
    s.normalizeLoudness = m_loud->isChecked();
    s.outputPath = m_path->text().trimmed();
    const Project &p = m_doc->project();
    const QString range = m_range->currentData().toString();
    if (range == "inout" && p.inPoint >= 0 && p.outPoint > p.inPoint) {
        s.rangeStart = p.inPoint;
        s.rangeEnd = p.outPoint;
    } else if (range == "selection" && !m_doc->selectedClips().isEmpty()) {
        TimeUs a = std::numeric_limits<TimeUs>::max(), b = 0;
        for (const QString &id : m_doc->selectedClips())
            if (const Clip *c = p.findClip(id)) {
                a = std::min(a, c->start);
                b = std::max(b, c->end());
            }
        if (b > a) {
            s.rangeStart = a;
            s.rangeEnd = b;
        }
    }
    return s;
}

void ExportDialog::updateEstimate()
{
    m_crfLabel->setText(QString::number(m_crf->value()));
    const ExportSettings s = collect();
    const TimeUs end = s.rangeEnd > 0 ? s.rangeEnd : m_doc->project().duration();
    const double secs = std::max<TimeUs>(0, end - s.rangeStart) / 1e6;
    int kbps = s.rateMode == "bitrate" ? s.videoBitrateKbps : ExportGraph::estimateBitrateKbps(s.width, s.height, s.fps, s.crf);
    if (s.videoCodec == "hevc" && s.rateMode == "crf")
        kbps = kbps * 6 / 10;
    const double mb = (kbps + s.audioBitrateKbps) * secs / 8.0 / 1024.0;
    m_estimate->setText(QString("Duration %1 · estimated file size ≈ %2 MB").arg(formatClock(TimeUs(secs * 1e6), false)).arg(int(mb)));
}

void ExportDialog::setRunning(bool running)
{
    m_form->setEnabled(!running);
    m_export->setEnabled(!running);
    m_cancel->setVisible(running);
}

void ExportDialog::startExport()
{
    if (m_job)
        return;
    ExportSettings s = collect();
    if (s.outputPath.isEmpty()) {
        m_status->setText("Choose where to save the video.");
        return;
    }
    if (QFileInfo(s.outputPath).suffix().isEmpty())
        s.outputPath += ".mp4";
    if (QFileInfo::exists(s.outputPath) &&
        QMessageBox::question(this, "Export", QFileInfo(s.outputPath).fileName() + " already exists. Replace it?") != QMessageBox::Yes)
        return;
    if (Tools::ffmpeg().isEmpty()) {
        m_status->setText("FFmpeg was not found. Install it or set its location in Preferences → Tools.");
        return;
    }
    const Project &p = m_doc->project();
    for (const MediaAsset &m : p.media)
        if (m.missing) {
            if (QMessageBox::question(this, "Export", "Some media is offline and will be skipped. Export anyway?") != QMessageBox::Yes)
                return;
            break;
        }
    // remember settings in the project
    m_doc->updateMeta([&](Project &pr) { pr.exportSettings = s.toJson(); });

    ExportRequest req;
    req.project = m_doc->project();
    req.projectDir = m_doc->projectDir();
    req.settings = s;
    req.overlay = OverlayScene::build(req.project, s.burnCaptions, true);
    if (s.writeSrt || s.writeVtt) {
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Caption && !t.hidden) {
                const CaptionStyle st = TemplateLibrary::instance().get(t.styleId).withOverrides(t.styleOverrides);
                const TimeUs end = s.rangeEnd > 0 ? s.rangeEnd : p.duration();
                const auto cues = SubtitleIO::clipRange(CaptionMapper::map(p, t), s.rangeStart, end);
                if (s.writeSrt)
                    req.srtText = SubtitleIO::toSrt(cues, st);
                if (s.writeVtt)
                    req.vttText = SubtitleIO::toVtt(cues, st, p.speakers, st.wordEffect != "none");
                break;
            }
    }
    m_job = new ExportJob(std::move(req));
    m_lastOutput = s.outputPath;
    m_reveal->hide();
    m_log->hide();
    m_progress->setValue(0);
    m_status->setText("Preparing render…");
    setRunning(true);
    connect(m_job, &ExportJob::progressChanged, this, [this](double frac, double fps, double eta, const QString &status) {
        m_progress->setValue(int(frac * 1000));
        QString t = QString("%1 · %2%").arg(status).arg(int(frac * 100));
        if (fps > 0)
            t += QString(" · %1 fps").arg(fps, 0, 'f', 1);
        if (eta > 0)
            t += QString(" · about %1 left").arg(formatClock(TimeUs(eta * 1e6), false));
        m_status->setText(t);
    });
    connect(m_job, &ExportJob::succeeded, this, [this](const QString &out, double secs, const QStringList &warnings) {
        setRunning(false);
        m_progress->setValue(1000);
        QString msg = QString("Exported %1 in %2 s.").arg(QFileInfo(out).fileName()).arg(secs, 0, 'f', 1);
        if (!warnings.isEmpty())
            msg += "\n" + warnings.join("\n");
        m_status->setText(msg);
        m_reveal->show();
    });
    connect(m_job, &ExportJob::failedWithReport, this, [this](const QString &msg, const QString &log) {
        setRunning(false);
        m_progress->setValue(0);
        m_lastLog = log;
        m_status->setText("Export failed: " + msg);
        m_log->show();
    });
    connect(m_job, &ExportJob::cancelledByUser, this, [this]() {
        setRunning(false);
        m_progress->setValue(0);
        m_status->setText("Export cancelled. No partial file was left behind.");
    });
    connect(m_job, &QThread::finished, m_job, &QObject::deleteLater);
    m_job->start();
}

void ExportDialog::reject()
{
    if (m_job && m_job->isRunning()) {
        if (QMessageBox::question(this, "Export", "An export is running. Cancel it?") != QMessageBox::Yes)
            return;
        m_job->cancel();
        m_job->wait(10000);
    }
    QDialog::reject();
}

void ExportDialog::closeEvent(QCloseEvent *e)
{
    if (m_job && m_job->isRunning()) {
        e->ignore();
        reject();
        return;
    }
    QDialog::closeEvent(e);
}

} // namespace ss
