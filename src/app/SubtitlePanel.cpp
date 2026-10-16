#include "SubtitlePanel.h"
#include "../captions/SubtitleIO.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "TemplateGallery.h"
#include "Theme.h"
#include "Widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QResizeEvent>
#include <QSettings>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace ss {

SubtitlePanel::SubtitlePanel(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    m_job = new TranscriptionJob(this);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    m_tabs = new QTabWidget;
    v->addWidget(m_tabs);
    auto *gen = new QWidget;
    auto *edit = new QWidget;
    auto *style = new QWidget;
    buildGenerateTab(gen);
    buildEditTab(edit);
    buildStyleTab(style);
    auto scrolled = [](QWidget *w) {
        auto *sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(w);
        return sa;
    };
    m_tabs->addTab(scrolled(gen), "Generate");
    m_tabs->addTab(scrolled(edit), "Edit captions");
    m_tabs->addTab(style, "Style");
    m_tabs->setUsesScrollButtons(true);

    connect(doc, &Document::projectChanged, this, [this]() {
        // Hidden behind another tab: refresh when it is shown instead of on every edit.
        if (isVisible())
            refreshAll();
        else
            m_refreshPending = true;
    });
    connect(doc, &Document::projectReset, this, [this]() {
        m_activeTrack.clear();
        refreshAll();
    });
    connect(doc, &Document::playheadChanged, this, &SubtitlePanel::syncToPlayhead);
    connect(doc, &Document::cueSelectionChanged, this, [this]() {
        if (!m_doc->selectedCaptionTrack().isEmpty() && m_doc->selectedCaptionTrack() != m_activeTrack) {
            m_activeTrack = m_doc->selectedCaptionTrack();
            refreshAll();
        }
        const int cue = m_doc->selectedCue();
        if (perCaptionMode())
            refreshStyle(); // the style controls show this caption's own look
        if (cue >= 0 && cue < m_table->rowCount() && m_table->currentRow() != cue) {
            m_updating = true;
            m_table->selectRow(cue);
            m_table->scrollToItem(m_table->item(cue, 0));
            m_updating = false;
            refreshWords();
        }
    });

    connect(m_job, &TranscriptionJob::stageChanged, this, [this](TranscriptionJob::Stage, const QString &label) {
        m_status->setText(label + "…");
        emit generationStateChanged(m_job->isRunning(), m_progress->value(), label);
    });
    connect(m_job, &TranscriptionJob::progress, this, [this](int p) {
        m_progress->setValue(p);
        emit generationStateChanged(true, p, m_status->text());
    });
    connect(m_job, &TranscriptionJob::finished, this, &SubtitlePanel::onFinished);
    connect(m_job, &TranscriptionJob::failed, this, [this](const QString &msg, const QString &details) {
        m_lastLog = details;
        m_error->setText(msg);
        m_error->show();
        m_retry->show();
        m_generate->setEnabled(true);
        m_cancel->hide();
        m_status->setText("Caption generation failed");
        emit generationStateChanged(false, 0, "Caption generation failed: " + msg);
    });
    connect(m_job, &TranscriptionJob::cancelled, this, [this]() {
        m_generate->setEnabled(true);
        m_cancel->hide();
        m_progress->setValue(0);
        m_status->setText("Cancelled");
        emit generationStateChanged(false, 0, "Caption generation cancelled");
    });
    refreshAll();
}

// ------------------------------------------------------------------ helpers

const Track *SubtitlePanel::captionTrack() const
{
    if (!m_doc->isOpen())
        return nullptr;
    const Project &p = m_doc->project();
    if (const Track *t = p.findTrack(m_activeTrack))
        if (t->kind == TrackKind::Caption)
            return t;
    for (const auto &t : p.tracks)
        if (t.kind == TrackKind::Caption)
            return &t;
    return nullptr;
}

QString SubtitlePanel::currentCaptionTrack() const
{
    const Track *t = captionTrack();
    return t ? t->id : QString();
}

const Transcript *SubtitlePanel::transcript() const
{
    const Track *t = captionTrack();
    return t ? m_doc->project().findTranscript(t->transcriptId) : nullptr;
}

QVector<CueInstance> SubtitlePanel::instances() const
{
    const Track *t = captionTrack();
    return t ? CaptionMapper::map(m_doc->project(), *t) : QVector<CueInstance>();
}

int SubtitlePanel::selectedCue() const
{
    return m_table->currentRow();
}

int SubtitlePanel::selectedWord() const
{
    const int r = m_words->currentRow();
    return r >= 0 && m_words->item(r, 0) ? m_words->item(r, 0)->data(Qt::UserRole).toInt() : -1;
}

bool SubtitlePanel::editTranscript(const QString &what, const std::function<bool(Transcript &, Project &)> &fn,
                                   const QString &mergeKey)
{
    const Track *t = captionTrack();
    if (!t)
        return false;
    const QString trackId = t->id;
    return m_doc->apply(what, [&](Project &p) {
        Track *tt = p.findTrack(trackId);
        Transcript *tr = tt ? p.findTranscript(tt->transcriptId) : nullptr;
        return tr && fn(*tr, p);
    }, mergeKey);
}

void SubtitlePanel::showTab(int index)
{
    m_tabs->setCurrentIndex(index);
    if (index == 1 && m_table->currentRow() < 0 && m_table->rowCount() > 0) {
        QString tid;
        int cue = -1;
        captionAt(m_doc->playhead(), &tid, &cue, nullptr);
        m_table->selectRow(cue >= 0 && cue < m_table->rowCount() ? cue : 0);
    }
}

bool SubtitlePanel::isGenerating() const
{
    return m_job->isRunning();
}

// ------------------------------------------------------------------ generate tab

void SubtitlePanel::buildGenerateTab(QWidget *tab)
{
    auto *v = new QVBoxLayout(tab);
    auto *intro = new QLabel("Captions are transcribed <b>on this computer</b> with a local speech model. "
                             "No audio, video or text is uploaded.");
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto *f = new QFormLayout;
    m_source = new QComboBox;
    m_source->setToolTip("Media whose speech will be transcribed");
    f->addRow("Source", m_source);
    m_language = new QComboBox;
    for (const LanguageOption &l : Languages::all())
        m_language->addItem(l.label, l.code);
    m_language->setCurrentIndex(std::max(0, m_language->findData(Settings::instance().defaultLanguage)));
    f->addRow("Language", m_language);
    auto *langNote = new QLabel;
    langNote->setObjectName("hint");
    langNote->setWordWrap(true);
    f->addRow("", langNote);
    connect(m_language, &QComboBox::currentIndexChanged, this, [this, langNote](int) {
        langNote->setText(Languages::byCode(m_language->currentData().toString()).note);
        Settings::instance().defaultLanguage = m_language->currentData().toString();
        Settings::instance().save();
    });
    langNote->setText(Languages::byCode(m_language->currentData().toString()).note);
    auto *modelRow = new QHBoxLayout;
    m_model = new QComboBox;
    auto *manage = new QPushButton("Manage…");
    modelRow->addWidget(m_model, 1);
    modelRow->addWidget(manage);
    f->addRow("Speech model", modelRow);
    m_vadModel = new QComboBox;
    m_vadModel->setToolTip("Optional voice-activity model; skips silence and reduces hallucinated text");
    f->addRow("Speech detection", m_vadModel);
    m_vocab = new QLineEdit;
    m_vocab->setPlaceholderText("Names, brands or terms (helps spelling), e.g. ShiftrMart, Vegas Capital");
    f->addRow("Vocabulary", m_vocab);
    m_refine = new QCheckBox("Refine word timing to speech onsets");
    m_refine->setChecked(Settings::instance().refineWordTiming);
    f->addRow("", m_refine);
    m_translate = new QCheckBox("Translate to English (captions in English for any spoken language)");
    m_translate->setToolTip("Uses the speech model's built-in translation, on this computer");
    f->addRow("", m_translate);
    v->addLayout(f);

    auto *row = new QHBoxLayout;
    m_generate = new QPushButton("Generate captions");
    m_generate->setObjectName("primary");
    m_cancel = new QPushButton("Cancel");
    m_cancel->hide();
    m_retry = new QPushButton("Retry");
    m_retry->hide();
    auto *details = new QPushButton("Details");
    row->addWidget(m_generate);
    row->addWidget(m_cancel);
    row->addWidget(m_retry);
    row->addStretch();
    row->addWidget(details);
    v->addLayout(row);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    v->addWidget(m_progress);
    m_status = new QLabel("Ready");
    m_status->setObjectName("hint");
    m_status->setWordWrap(true);
    v->addWidget(m_status);
    m_error = new QLabel;
    m_error->setObjectName("error");
    m_error->setWordWrap(true);
    m_error->hide();
    v->addWidget(m_error);

    v->addWidget(hline());
    auto *imp = new QPushButton("Import subtitles (SRT / VTT)…");
    v->addWidget(imp);
    v->addStretch(1);

    connect(manage, &QPushButton::clicked, this, &SubtitlePanel::manageModelsRequested);
    connect(m_generate, &QPushButton::clicked, this, [this]() {
        generate(m_source->currentData().toString(), m_language->currentData().toString());
    });
    connect(m_retry, &QPushButton::clicked, m_generate, &QPushButton::click);
    connect(m_cancel, &QPushButton::clicked, m_job, &TranscriptionJob::cancel);
    connect(details, &QPushButton::clicked, this, [this]() {
        QMessageBox box(this);
        box.setWindowTitle("Transcription details");
        box.setText("Log of the last caption generation (stays on this computer).");
        box.setDetailedText(m_lastLog.isEmpty() ? m_job->log() : m_lastLog);
        box.exec();
    });
    connect(imp, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, "Import subtitles", QString(), "Subtitles (*.srt *.vtt)");
        if (!path.isEmpty())
            importSubtitles(path);
    });
    connect(m_model, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_updating && !m_model->currentData().toString().isEmpty()) {
            Settings::instance().defaultModel = m_model->currentData().toString();
            Settings::instance().save();
        }
    });
    reloadModels();
}

void SubtitlePanel::reloadModels()
{
    m_updating = true;
    m_model->clear();
    for (const ModelInfo &m : ModelManager::instance().speechModels())
        m_model->addItem(QString("%1 (%2)").arg(m.name, ModelManager::describeSize(m.size)), m.file);
    if (m_model->count() == 0)
        m_model->addItem("No model installed — click Manage…", QString());
    const int idx = m_model->findData(Settings::instance().defaultModel);
    if (idx >= 0)
        m_model->setCurrentIndex(idx);
    m_vadModel->clear();
    m_vadModel->addItem("Off", QString());
    for (const ModelInfo &m : ModelManager::instance().vadModels())
        m_vadModel->addItem(m.name, m.file);
    if (Settings::instance().useVad && m_vadModel->count() > 1)
        m_vadModel->setCurrentIndex(1);
    m_updating = false;
}

void SubtitlePanel::refreshSources()
{
    const QString cur = m_source->currentData().toString();
    m_source->clear();
    if (!m_doc->isOpen())
        return;
    for (const MediaAsset &m : m_doc->project().media)
        if (m.hasAudio)
            m_source->addItem(m.name + (m.missing ? " (offline)" : ""), m.id);
    const int idx = m_source->findData(cur);
    if (idx >= 0)
        m_source->setCurrentIndex(idx);
    m_generate->setEnabled(m_source->count() > 0 && !m_job->isRunning());
    if (m_source->count() == 0)
        m_status->setText("Import a video or audio file with speech to generate captions.");
}

void SubtitlePanel::generate(const QString &mediaId, const QString &languageCode)
{
    if (m_job->isRunning() || !m_doc->isOpen())
        return;
    const MediaAsset *m = m_doc->project().findMedia(mediaId);
    if (!m) {
        m_status->setText("Choose a source with speech first.");
        return;
    }
    if (m_model->currentData().toString().isEmpty()) {
        m_error->setText("No speech model is installed. Click Manage… to add a whisper.cpp model file "
                         "(for example ggml-small.bin) from this computer.");
        m_error->show();
        m_tabs->setCurrentIndex(0);
        emit manageModelsRequested();
        return;
    }
    m_pendingMediaId = mediaId;
    const int li = m_language->findData(languageCode);
    if (li >= 0)
        m_language->setCurrentIndex(li);
    TranscriptionRequest req;
    req.mediaId = mediaId;
    req.mediaPath = m->path;
    req.mediaDuration = m->duration;
    req.languageCode = languageCode;
    req.modelFile = m_model->currentData().toString();
    req.vadModelFile = m_vadModel->currentData().toString();
    req.extraPrompt = m_vocab->text();
    req.threads = Settings::instance().aiThreads;
    req.useGpu = Settings::instance().aiUseGpu;
    req.refineTiming = m_refine->isChecked();
    req.translateToEnglish = m_translate && m_translate->isChecked();
    req.workDir = m_doc->projectDir() + "/subtitles/work";
    const Track *t = captionTrack();
    req.styleForSegmentation = TemplateLibrary::instance().get(t ? t->styleId : "bold-highlight-yellow");
    Settings::instance().refineWordTiming = req.refineTiming;
    Settings::instance().save();
    m_error->hide();
    m_retry->hide();
    m_generate->setEnabled(false);
    m_cancel->show();
    m_progress->setValue(0);
    m_job->start(req);
}

void SubtitlePanel::onFinished(const Transcript &tIn, const QString &summary)
{
    m_generate->setEnabled(true);
    m_cancel->hide();
    m_status->setText(summary);
    emit generationStateChanged(false, 100, "Captions ready: " + summary);
    if (!m_doc->isOpen())
        return;
    QString createdTrack;
    m_doc->apply("Generate captions", [&](Project &p) {
        if (!p.findMedia(tIn.mediaId))
            return false;
        Transcript t = tIn;
        // Replace the transcript of an existing caption track for this media (keeps its style).
        for (auto &tr : p.tracks) {
            if (tr.kind != TrackKind::Caption)
                continue;
            Transcript *old = p.findTranscript(tr.transcriptId);
            if (old && old->mediaId == t.mediaId) {
                t.id = old->id;
                t.cues = Segmenter::segment(t.words, SegmentParams::fromStyle(
                                                         TemplateLibrary::instance().get(tr.styleId).withOverrides(tr.styleOverrides)));
                *old = t;
                createdTrack = tr.id;
                return true;
            }
        }
        p.transcripts << t;
        Track ct;
        ct.id = newId();
        ct.kind = TrackKind::Caption;
        const MediaAsset *m = p.findMedia(t.mediaId);
        ct.name = "Captions · " + (m ? QFileInfo(m->name).completeBaseName().left(18) : QString("media"));
        ct.transcriptId = t.id;
        ct.styleId = p.canvas.height > p.canvas.width ? "bold-highlight-yellow" : "minimal-clean";
        const CaptionStyle style = TemplateLibrary::instance().get(ct.styleId);
        p.transcripts.last().cues = Segmenter::segment(t.words, SegmentParams::fromStyle(style));
        // insert after the last video track so it reads naturally in the timeline
        p.tracks << ct;
        createdTrack = ct.id;
        return true;
    });
    if (!createdTrack.isEmpty()) {
        m_activeTrack = createdTrack;
        refreshAll();
        m_tabs->setCurrentIndex(1);
        if (m_table->rowCount() > 0)
            m_table->selectRow(0);
        emit captionsReady();
    }
}

void SubtitlePanel::importSubtitles(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import subtitles", "Could not open " + path);
        return;
    }
    Transcript t;
    QString err;
    if (!SubtitleIO::readSubtitles(QString::fromUtf8(f.readAll()), t, &err)) {
        QMessageBox::warning(this, "Import subtitles", err);
        return;
    }
    t.id = newId();
    t.language = "imported";
    t.created = QDateTime::currentDateTimeUtc();
    QString trackId;
    m_doc->apply("Import subtitles", [&](Project &p) {
        p.transcripts << t;
        Track ct;
        ct.id = newId();
        ct.kind = TrackKind::Caption;
        ct.name = QFileInfo(path).completeBaseName().left(24);
        ct.transcriptId = t.id;
        ct.styleId = "minimal-box";
        p.tracks << ct;
        trackId = ct.id;
        return true;
    });
    m_activeTrack = trackId;
    refreshAll();
    emit statusMessage(QString("Imported %1 subtitles").arg(t.cues.size()));
}

void SubtitlePanel::exportSubtitles(const QString &format)
{
    const Track *t = captionTrack();
    if (!t) {
        QMessageBox::information(this, "Export subtitles", "There are no captions to export yet.");
        return;
    }
    const Project &p = m_doc->project();
    const QVector<CueInstance> cues = CaptionMapper::map(p, *t);
    const CaptionStyle style = TemplateLibrary::instance().get(t->styleId).withOverrides(t->styleOverrides);
    const QString ext = format == "txt" ? "txt" : format;
    const QString suggested = m_doc->projectDir() + "/exports/" + p.name + "." + ext;
    const QString path = QFileDialog::getSaveFileName(this, "Export subtitles", suggested,
                                                      QString("%1 (*.%2)").arg(format.toUpper(), ext));
    if (path.isEmpty())
        return;
    QString text;
    if (format == "srt")
        text = SubtitleIO::toSrt(cues, style);
    else if (format == "vtt")
        text = SubtitleIO::toVtt(cues, style, p.speakers, style.wordEffect != "none");
    else if (format == "ass")
        text = SubtitleIO::toAss(cues, style, p.canvas.width, p.canvas.height);
    else
        text = SubtitleIO::toPlainText(cues, true);
    QString err;
    if (!SubtitleIO::writeTextFile(path, text, &err))
        QMessageBox::warning(this, "Export subtitles", err);
    else
        emit statusMessage("Subtitles exported to " + path);
}

bool SubtitlePanel::captionAt(TimeUs t, QString *trackId, int *cue, QString *text) const
{
    if (!m_doc->isOpen())
        return false;
    const Project &p = m_doc->project();
    for (const Track &tr : p.tracks) {
        if (tr.kind != TrackKind::Caption || tr.hidden)
            continue;
        const auto inst = CaptionMapper::map(p, tr);
        if (const CueInstance *ci = CaptionMapper::instanceAt(inst, t)) {
            const Transcript *trs = p.findTranscript(tr.transcriptId);
            if (trackId)
                *trackId = tr.id;
            if (cue)
                *cue = ci->cueIndex;
            if (text)
                *text = trs ? trs->cueText(ci->cueIndex) : ci->text();
            return true;
        }
    }
    return false;
}

bool SubtitlePanel::editCaptionAt(TimeUs t, const QString &newText)
{
    QString trackId;
    int cue = -1;
    if (!captionAt(t, &trackId, &cue, nullptr))
        return false;
    const QString text = newText.simplified();
    return m_doc->apply("Edit caption text", [&](Project &p) {
        Track *tr = p.findTrack(trackId);
        Transcript *trs = tr ? p.findTranscript(tr->transcriptId) : nullptr;
        return trs && TranscriptEdit::setCueText(*trs, cue, text);
    });
}

// ------------------------------------------------------------------ edit tab

void SubtitlePanel::buildEditTab(QWidget *tab)
{
    auto *v = new QVBoxLayout(tab);
    v->setContentsMargins(4, 4, 4, 4);
    m_trackCombo = new QComboBox;
    m_trackCombo->setToolTip("Caption track");
    m_trackCombo->setMinimumContentsLength(10);
    m_trackCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    v->addWidget(m_trackCombo);
    auto *top = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search captions");
    m_search->setClearButtonEnabled(true);
    auto *next = new QPushButton("Find next");
    top->addWidget(m_search, 1);
    top->addWidget(next);
    v->addLayout(top);
    auto *hint = new QLabel("Click a caption to edit its text below. You can also double-click a caption in the list, "
                            "or double-click the caption on the video.");
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({"Time", "Length", "Speaker", "Caption"});
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::SelectedClicked);
    m_table->setAccessibleName("Caption list");
    m_table->setWordWrap(true);
    m_table->setMinimumHeight(150);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    m_table->setColumnHidden(1, true); // length is shown in the tooltip and the editor header
    m_table->setTextElideMode(Qt::ElideRight);
    v->addWidget(m_table, 3);

    // Direct caption text editor for the selected caption
    m_cueInfo = new QLabel("Select a caption to edit it");
    m_cueInfo->setObjectName("hint");
    v->addWidget(m_cueInfo);
    m_cueText = new QPlainTextEdit;
    m_cueText->setPlaceholderText("Caption text — edit and press ⌘/Ctrl+Enter or Apply");
    m_cueText->setMaximumHeight(72);
    m_cueText->setMinimumHeight(44);
    m_cueText->setTabChangesFocus(true);
    v->addWidget(m_cueText);
    auto *applyText = new QPushButton("Apply text");
    applyText->setObjectName("primary");
    auto *revertText = new QPushButton("Revert");
    auto *prevCue = new QPushButton("◀ Prev");
    auto *nextCue = new QPushButton("Next ▶");
    v->addWidget(flowRow({applyText, revertText, prevCue, nextCue}));

    // Secondary tools fold away so the core editing flow fits small screens.
    auto *moreToggle = new QToolButton;
    moreToggle->setText("Timing, speakers & export");
    moreToggle->setCheckable(true);
    moreToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    moreToggle->setArrowType(Qt::RightArrow);
    moreToggle->setStyleSheet("QToolButton { border: none; padding: 4px 0; font-weight: 600; }");
    v->addWidget(moreToggle);
    auto *moreBox = new QWidget;
    auto *mv = new QVBoxLayout(moreBox);
    mv->setContentsMargins(0, 0, 0, 0);
    v->addWidget(moreBox, 2);
    {
        QSettings qs;
        const bool open = qs.value("ui/captionToolsOpen", false).toBool();
        moreToggle->setChecked(open);
        moreToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        moreBox->setVisible(open);
    }
    connect(moreToggle, &QToolButton::toggled, this, [moreToggle, moreBox](bool on) {
        moreBox->setVisible(on);
        moreToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        QSettings().setValue("ui/captionToolsOpen", on);
    });

    QList<QWidget *> toolButtons;
    auto mk = [&](const QString &label, const QString &tip) {
        auto *b = new QPushButton(label);
        b->setToolTip(tip);
        toolButtons << b;
        return b;
    };
    auto *startEarlier = mk("◀ In", "Start one frame earlier (Alt+[)");
    auto *startLater = mk("In ▶", "Start one frame later");
    auto *endEarlier = mk("◀ Out", "End one frame earlier");
    auto *endLater = mk("Out ▶", "End one frame later (Alt+])");
    auto *split = mk("Split", "Split the caption before the selected word");
    auto *merge = mk("Merge", "Merge with the next caption");
    auto *del = mk("Delete", "Delete this caption and its words");
    mv->addWidget(flowRow(toolButtons));

    auto *spkRow = new QHBoxLayout;
    spkRow->addWidget(new QLabel("Speaker"));
    m_speaker = new QComboBox;
    spkRow->addWidget(m_speaker, 1);
    auto *speakers = new QPushButton("Edit speakers…");
    spkRow->addWidget(speakers);
    mv->addLayout(spkRow);

    m_words = new QTableWidget(0, 4);
    m_words->setHorizontalHeaderLabels({"Word", "Start", "End", "Confidence"});
    m_words->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_words->verticalHeader()->setVisible(false);
    m_words->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_words->setSelectionMode(QAbstractItemView::SingleSelection);
    m_words->setAccessibleName("Word timing");
    m_words->setMinimumHeight(80);
    m_words->horizontalHeader()->setMinimumSectionSize(40);
    mv->addWidget(new QLabel("Word timing (timeline time)"));
    mv->addWidget(m_words, 2);

    auto *reflow = new QPushButton("Re-flow captions");
    reflow->setToolTip("Rebuild caption breaks from the template rules; your text corrections and manual breaks are kept");
    auto *shift = new QSpinBox;
    shift->setRange(-60000, 60000);
    shift->setSingleStep(40);
    shift->setSuffix(" ms");
    auto *applyShift = new QPushButton("Shift all");
    applyShift->setToolTip("Move every caption earlier/later (sync offset)");
    mv->addWidget(flowRow({reflow, shift, applyShift}));
    QList<QWidget *> expRow{new QLabel("Export:")};
    for (const QString &fmt : QStringList{"srt", "vtt", "ass", "txt"}) {
        auto *b = new QPushButton(fmt.toUpper());
        expRow << b;
        connect(b, &QPushButton::clicked, this, [this, fmt]() { exportSubtitles(fmt); });
    }
    mv->addWidget(flowRow(expRow));

    auto applyCueText = [this]() {
        const int cue = selectedCue();
        if (cue < 0)
            return;
        const QString text = m_cueText->toPlainText().simplified();
        if (editTranscript("Edit caption text", [&](Transcript &t, Project &) { return TranscriptEdit::setCueText(t, cue, text); }))
            emit statusMessage("Caption updated");
    };
    connect(applyText, &QPushButton::clicked, this, applyCueText);
    auto *applyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_cueText);
    applyShortcut->setContext(Qt::WidgetShortcut);
    connect(applyShortcut, &QShortcut::activated, this, applyCueText);
    connect(revertText, &QPushButton::clicked, this, &SubtitlePanel::refreshWords);
    connect(prevCue, &QPushButton::clicked, this, [this]() {
        if (m_table->currentRow() > 0)
            m_table->selectRow(m_table->currentRow() - 1);
    });
    connect(nextCue, &QPushButton::clicked, this, [this]() {
        if (m_table->currentRow() + 1 < m_table->rowCount())
            m_table->selectRow(m_table->currentRow() + 1);
    });

    connect(m_trackCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        m_activeTrack = m_trackCombo->currentData().toString();
        refreshTable();
        refreshStyle();
    });
    auto findNext = [this]() {
        const Transcript *t = transcript();
        if (!t)
            return;
        const QVector<int> hits = TranscriptEdit::search(*t, m_search->text());
        if (hits.isEmpty()) {
            emit statusMessage("No captions match");
            return;
        }
        int pick = hits.first();
        for (int h : hits)
            if (h > m_table->currentRow()) {
                pick = h;
                break;
            }
        m_table->selectRow(pick);
    };
    connect(next, &QPushButton::clicked, this, findNext);
    connect(m_search, &QLineEdit::returnPressed, this, findNext);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_updating)
            return;
        const int cue = selectedCue();
        refreshWords();
        if (cue < 0)
            return;
        m_doc->setSelectedCue(currentCaptionTrack(), cue);
        for (const CueInstance &ci : instances())
            if (ci.cueIndex == cue) {
                emit seekRequested(ci.start);
                break;
            }
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != 3)
            return;
        const int cue = it->row();
        const QString text = it->text();
        editTranscript("Edit caption text", [&](Transcript &t, Project &) { return TranscriptEdit::setCueText(t, cue, text); });
    });
    auto nudge = [this](bool startEdge, int frames) {
        const int cue = selectedCue();
        if (cue < 0)
            return;
        const TimeUs d = frames * frameDurationUs(m_doc->project().canvas.fps);
        editTranscript("Nudge caption", [&](Transcript &t, Project &) {
            return TranscriptEdit::setCueTiming(t, cue, t.cueStart(cue) + (startEdge ? d : 0), t.cueEnd(cue) + (startEdge ? 0 : d));
        }, QString("nudge:%1:%2").arg(cue).arg(startEdge));
    };
    connect(startEarlier, &QPushButton::clicked, this, [nudge]() { nudge(true, -1); });
    connect(startLater, &QPushButton::clicked, this, [nudge]() { nudge(true, 1); });
    connect(endEarlier, &QPushButton::clicked, this, [nudge]() { nudge(false, -1); });
    connect(endLater, &QPushButton::clicked, this, [nudge]() { nudge(false, 1); });
    connect(split, &QPushButton::clicked, this, [this]() {
        const int cue = selectedCue(), word = selectedWord();
        if (cue < 0 || word < 0) {
            emit statusMessage("Select the word where the new caption should start");
            return;
        }
        editTranscript("Split caption", [&](Transcript &t, Project &) { return TranscriptEdit::splitCue(t, cue, word); });
    });
    connect(merge, &QPushButton::clicked, this, [this]() {
        const int cue = selectedCue();
        editTranscript("Merge captions", [&](Transcript &t, Project &) { return TranscriptEdit::mergeWithNext(t, cue); });
    });
    connect(del, &QPushButton::clicked, this, [this]() {
        const int cue = selectedCue();
        editTranscript("Delete caption", [&](Transcript &t, Project &) { return TranscriptEdit::deleteCue(t, cue); });
    });
    connect(m_speaker, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating)
            return;
        const int cue = selectedCue();
        const int spk = m_speaker->currentData().toInt();
        editTranscript("Set speaker", [&](Transcript &t, Project &) { return TranscriptEdit::setCueSpeaker(t, cue, spk); });
    });
    connect(speakers, &QPushButton::clicked, this, [this]() {
        const QString current = [&] {
            QStringList l;
            for (const Speaker &s : m_doc->project().speakers)
                l << s.name;
            return l.join(", ");
        }();
        bool ok = false;
        const QString names = QInputDialog::getText(this, "Speakers", "Speaker names, separated by commas:", QLineEdit::Normal,
                                                    current, &ok);
        if (!ok)
            return;
        static const QStringList palette{"#FFD43B", "#4DABF7", "#FF8787", "#69DB7C", "#DA77F2", "#FFA94D"};
        m_doc->apply("Edit speakers", [&](Project &p) {
            const QStringList list = names.split(',', Qt::SkipEmptyParts);
            QVector<Speaker> sp;
            for (int i = 0; i < list.size(); ++i) {
                Speaker s;
                s.id = i;
                s.name = list[i].trimmed();
                s.color = i < p.speakers.size() ? p.speakers[i].color : QColor(palette[i % palette.size()]);
                sp << s;
            }
            p.speakers = sp;
            return true;
        });
    });
    connect(m_words, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating)
            return;
        const int wi = m_words->item(it->row(), 0)->data(Qt::UserRole).toInt();
        if (it->column() == 0) {
            const QString text = it->text();
            editTranscript("Edit word", [&](Transcript &t, Project &) { return TranscriptEdit::setWordText(t, wi, text); });
        } else if (it->column() == 1 || it->column() == 2) {
            TimeUs tl;
            if (!TimeEdit::parse(it->text(), m_doc->project().canvas.fps, &tl)) {
                refreshWords();
                return;
            }
            const bool isStart = it->column() == 1;
            editTranscript("Edit word timing", [&](Transcript &t, Project &p) {
                TimeUs src;
                if (!CaptionMapper::timelineToTranscript(p, t, tl, &src))
                    return false;
                return TranscriptEdit::setWordTiming(t, wi, isStart ? src : t.words[wi].start, isStart ? t.words[wi].end : src);
            });
        }
    });
    connect(m_words, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_updating)
            return;
        const int wi = selectedWord();
        for (const CueInstance &ci : instances())
            for (const WordInstance &w : ci.words)
                if (w.wordIndex == wi) {
                    emit seekRequested(w.start);
                    return;
                }
    });
    connect(reflow, &QPushButton::clicked, this, [this]() {
        const Track *t = captionTrack();
        if (!t)
            return;
        const CaptionStyle style = TemplateLibrary::instance().get(t->styleId).withOverrides(t->styleOverrides);
        editTranscript("Re-flow captions", [&](Transcript &tr, Project &) {
            TranscriptEdit::resegment(tr, style);
            return true;
        });
    });
    connect(applyShift, &QPushButton::clicked, this, [this, shift]() {
        const TimeUs d = TimeUs(shift->value()) * kUsPerMs;
        editTranscript("Shift captions", [&](Transcript &t, Project &) { return TranscriptEdit::shiftAll(t, d); });
    });
}

void SubtitlePanel::refreshTrackCombo()
{
    m_updating = true;
    m_trackCombo->clear();
    if (m_doc->isOpen())
        for (const auto &t : m_doc->project().tracks)
            if (t.kind == TrackKind::Caption)
                m_trackCombo->addItem(t.name, t.id);
    const Track *ct = captionTrack();
    if (ct)
        m_trackCombo->setCurrentIndex(m_trackCombo->findData(ct->id));
    m_updating = false;
}

void SubtitlePanel::refreshTable()
{
    m_updating = true;
    const int keep = m_table->currentRow();
    m_table->setRowCount(0);
    const Transcript *t = transcript();
    if (!t) {
        m_updating = false;
        refreshWords();
        return;
    }
    const auto inst = instances();
    QHash<int, const CueInstance *> first;
    for (const auto &ci : inst)
        if (!first.contains(ci.cueIndex))
            first.insert(ci.cueIndex, &ci);
    const Project &p = m_doc->project();
    m_table->setRowCount(t->cues.size());
    for (int i = 0; i < t->cues.size(); ++i) {
        const CueInstance *ci = first.value(i, nullptr);
        QString shortTime = "—";
        if (ci) {
            const qint64 cs = ci->start / 10000;
            shortTime = QString("%1:%2.%3").arg(cs / 6000).arg((cs / 100) % 60, 2, 10, QChar('0')).arg((cs % 100) / 10);
        }
        auto *time = new QTableWidgetItem(shortTime);
        time->setToolTip(ci ? QString("%1 → %2 (%3 s)").arg(formatClock(ci->start), formatClock(ci->end))
                                  .arg((ci->end - ci->start) / 1e6, 0, 'f', 2)
                            : QString("This caption's words were trimmed off the timeline"));
        time->setFlags(time->flags() & ~Qt::ItemIsEditable);
        auto *len = new QTableWidgetItem(ci ? QString::number((ci->end - ci->start) / 1e6, 'f', 2) + " s" : QString());
        len->setFlags(len->flags() & ~Qt::ItemIsEditable);
        const Speaker *sp = p.findSpeaker(t->cues[i].speaker);
        auto *spk = new QTableWidgetItem(sp ? sp->name : QString());
        spk->setFlags(spk->flags() & ~Qt::ItemIsEditable);
        if (sp)
            spk->setForeground(sp->color);
        auto *text = new QTableWidgetItem(t->cueText(i));
        QStringList low;
        for (int w = t->cues[i].first; w <= t->cues[i].last && w < t->words.size(); ++w)
            if (t->words[w].confidence >= 0 && t->words[w].confidence < 0.5)
                low << t->words[w].text;
        if (!low.isEmpty()) {
            text->setForeground(QColor("#FFA94D"));
            text->setToolTip("Low confidence — please review: " + low.join(", "));
        }
        if (!ci)
            time->setForeground(Theme::textDim());
        m_table->setItem(i, 0, time);
        m_table->setItem(i, 1, len);
        m_table->setItem(i, 2, spk);
        m_table->setItem(i, 3, text);
    }
    m_table->resizeColumnToContents(0);
    bool anySpeaker = false;
    for (const Cue &cu : t->cues)
        anySpeaker = anySpeaker || cu.speaker >= 0;
    m_table->setProperty("anySpeaker", anySpeaker);
    m_table->setColumnHidden(2, !anySpeaker || width() < 440);
    if (keep >= 0 && keep < m_table->rowCount())
        m_table->selectRow(keep);
    m_updating = false;
    refreshWords();
}

void SubtitlePanel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_table->setColumnHidden(2, !m_table->property("anySpeaker").toBool() || width() < 440);
}

void SubtitlePanel::refreshWords()
{
    m_updating = true;
    m_words->setRowCount(0);
    m_speaker->clear();
    const Transcript *t = transcript();
    const int cue = selectedCue();
    if (!t || cue < 0 || cue >= t->cues.size()) {
        m_cueText->clear();
        m_cueText->setEnabled(false);
        m_cueInfo->setText(t ? "Select a caption to edit it" : "Generate or import captions first");
        m_updating = false;
        return;
    }
    m_cueText->setEnabled(true);
    if (!m_cueText->hasFocus())
        m_cueText->setPlainText(t->cueText(cue));
    m_cueInfo->setText(QString("Caption %1 of %2 · %3 s")
                           .arg(cue + 1)
                           .arg(t->cues.size())
                           .arg((t->cueEnd(cue) - t->cueStart(cue)) / 1e6, 0, 'f', 2));
    m_speaker->addItem("—", -1);
    for (const Speaker &s : m_doc->project().speakers)
        m_speaker->addItem(s.name, s.id);
    m_speaker->setCurrentIndex(std::max(0, m_speaker->findData(t->cues[cue].speaker)));
    QHash<int, WordInstance> mapped;
    for (const CueInstance &ci : instances())
        if (ci.cueIndex == cue)
            for (const WordInstance &w : ci.words)
                if (!mapped.contains(w.wordIndex))
                    mapped.insert(w.wordIndex, w);
    const Cue &c = t->cues[cue];
    m_words->setRowCount(c.last - c.first + 1);
    for (int w = c.first; w <= c.last && w < t->words.size(); ++w) {
        const int r = w - c.first;
        const Word &word = t->words[w];
        auto *it = new QTableWidgetItem(word.text);
        it->setData(Qt::UserRole, w);
        const bool on = mapped.contains(w);
        auto *s = new QTableWidgetItem(on ? formatClock(mapped[w].start) : "—");
        auto *e = new QTableWidgetItem(on ? formatClock(mapped[w].end) : "—");
        if (!on) {
            s->setFlags(s->flags() & ~Qt::ItemIsEditable);
            e->setFlags(e->flags() & ~Qt::ItemIsEditable);
        }
        auto *conf = new QTableWidgetItem(word.confidence >= 0 ? QString("%1%").arg(int(word.confidence * 100)) : "—");
        conf->setFlags(conf->flags() & ~Qt::ItemIsEditable);
        if (word.confidence >= 0 && word.confidence < 0.5)
            conf->setForeground(QColor("#FFA94D"));
        if (word.edited)
            it->setToolTip("Corrected by you");
        m_words->setItem(r, 0, it);
        m_words->setItem(r, 1, s);
        m_words->setItem(r, 2, e);
        m_words->setItem(r, 3, conf);
    }
    m_updating = false;
}

void SubtitlePanel::syncToPlayhead(TimeUs t)
{
    if (!isVisible() || m_tabs->currentIndex() != 1 || m_table->hasFocus() || m_table->isAncestorOf(QApplication::focusWidget()))
        return;
    const auto inst = instances();
    if (const CueInstance *ci = CaptionMapper::instanceAt(inst, t)) {
        if (m_table->currentRow() != ci->cueIndex && ci->cueIndex < m_table->rowCount()) {
            m_updating = true;
            m_table->selectRow(ci->cueIndex);
            m_table->scrollToItem(m_table->item(ci->cueIndex, 0), QAbstractItemView::PositionAtCenter);
            m_updating = false;
            refreshWords();
        }
    }
}

// ------------------------------------------------------------------ style tab

void SubtitlePanel::applyTemplate(const QString &id)
{
    const Track *t = captionTrack();
    if (!t) {
        emit statusMessage("Generate or import captions first");
        return;
    }
    const QString trackId = t->id;
    const int cue = m_doc->selectedCaptionTrack() == trackId ? m_doc->selectedCue() : -1;
    const bool onlyCue = m_styleForm && m_styleForm->property("applyToCue").toBool() && cue >= 0;
    const CaptionStyle s = TemplateLibrary::instance().get(id);
    m_doc->apply(onlyCue ? "Apply template to caption" : "Apply caption template", [&](Project &p) {
        Track *tt = p.findTrack(trackId);
        Transcript *tr = p.findTranscript(tt->transcriptId);
        if (onlyCue) {
            if (!tr || cue >= tr->cues.size())
                return false;
            QJsonObject o = s.toJson();
            o.remove("id");
            o.remove("name");
            o.remove("family");
            o.remove("description");
            // A look, not a placement: keep where this caption sits (its own position if it
            // was moved, otherwise wherever all captions are), so it keeps following the others.
            for (const char *k : {"offsetXPct", "offsetYPct", "anchor", "safeArea"})
                o.remove(k);
            const QJsonObject old = tr->cues[cue].styleOverrides;
            for (const char *k : {"offsetXPct", "offsetYPct"})
                if (old.contains(k))
                    o[k] = old.value(k);
            tr->cues[cue].styleOverrides = o;
            return true;
        }
        tt->styleId = id;
        // Re-flow with the new template's line rules; never re-transcribes, keeps corrections.
        if (tr)
            TranscriptEdit::resegment(*tr, s.withOverrides(tt->styleOverrides));
        return true;
    });
}

bool SubtitlePanel::perCaptionMode() const
{
    const Track *t = captionTrack();
    return t && m_styleForm && m_styleForm->property("applyToCue").toBool() && m_doc->selectedCaptionTrack() == t->id
           && m_doc->selectedCue() >= 0;
}

void SubtitlePanel::applyStyleOverride(const QString &key, const QJsonValue &v)
{
    if (m_updating)
        return;
    const Track *t = captionTrack();
    if (!t)
        return;
    const QString trackId = t->id;
    static const QStringList trackOnly{"maxWordsPerLine", "maxLines", "maxCharsPerLine"};
    if (perCaptionMode() && !trackOnly.contains(key)) {
        // change the look of the selected caption only
        const int cue = m_doc->selectedCue();
        m_doc->apply("Caption style (this caption)", [&](Project &p) {
            Track *tt = p.findTrack(trackId);
            Transcript *tr = tt ? p.findTranscript(tt->transcriptId) : nullptr;
            if (!tr || cue < 0 || cue >= tr->cues.size())
                return false;
            tr->cues[cue].styleOverrides[key] = v;
            return true;
        }, "cuestyle:" + trackId + ":" + QString::number(cue) + ":" + key);
        return;
    }
    m_doc->apply("Caption style", [&](Project &p) {
        Track *tt = p.findTrack(trackId);
        tt->styleOverrides[key] = v;
        if (key == "maxWordsPerLine" || key == "maxLines" || key == "maxCharsPerLine")
            if (Transcript *tr = p.findTranscript(tt->transcriptId))
                TranscriptEdit::resegment(*tr, TemplateLibrary::instance().get(tt->styleId).withOverrides(tt->styleOverrides));
        return true;
    }, "style:" + trackId + ":" + key);
}

void SubtitlePanel::buildStyleTab(QWidget *tab)
{
    auto *v = new QVBoxLayout(tab);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("Templates"));
    auto *applyCue = new QCheckBox("Change only the selected caption");
    applyCue->setToolTip("When on, templates and every style setting below (font, size, colours, position…) change just the "
                         "caption selected in the list or on the video");
    row->addStretch();
    row->addWidget(applyCue);
    v->addLayout(row);
    m_gallery = new TemplateGallery;
    m_gallery->setMinimumHeight(150);
    v->addWidget(m_gallery, 2);
    connect(m_gallery, &TemplateGallery::templateChosen, this, &SubtitlePanel::applyTemplate);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_styleForm = new QWidget;
    connect(applyCue, &QCheckBox::toggled, m_styleForm, [this](bool on) {
        m_styleForm->setProperty("applyToCue", on);
        refreshStyle();
    });
    auto *f = new QFormLayout(m_styleForm);
    f->setLabelAlignment(Qt::AlignRight);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);
    scroll->setWidget(m_styleForm);
    v->addWidget(scroll, 3);

    auto style = [this]() -> CaptionStyle {
        const Track *t = captionTrack();
        if (!t)
            return CaptionStyle();
        CaptionStyle st = TemplateLibrary::instance().get(t->styleId).withOverrides(t->styleOverrides);
        // "only the selected caption": show that caption's own look
        if (perCaptionMode())
            if (const Transcript *tr = m_doc->project().findTranscript(t->transcriptId); tr && m_doc->selectedCue() < tr->cues.size())
                st = st.withOverrides(tr->cues[m_doc->selectedCue()].styleOverrides);
        return st;
    };

    auto *font = new QFontComboBox;
    f->addRow("Font", font);
    connect(font, &QFontComboBox::currentFontChanged, this, [this, style](const QFont &fnt) {
        QStringList fams = style().fontFamilies;
        fams.removeAll(fnt.family());
        fams.prepend(fnt.family());
        applyStyleOverride("fontFamilies", QJsonArray::fromStringList(fams));
    });
    auto *weight = new QComboBox;
    for (int w : {300, 400, 500, 600, 700, 800, 900})
        weight->addItem(QString::number(w), w);
    f->addRow("Weight", weight);
    connect(weight, &QComboBox::currentIndexChanged, this, [this, weight](int) { applyStyleOverride("fontWeight", weight->currentData().toInt()); });
    auto *size = new SliderSpin(0.02, 0.15, 0.001, 3);
    f->addRow("Size", size);
    connect(size, &SliderSpin::valueChanged, this, [this](double v) { applyStyleOverride("sizePct", v); });
    auto *upper = new QCheckBox("UPPERCASE");
    f->addRow("", upper);
    connect(upper, &QCheckBox::toggled, this, [this](bool on) { applyStyleOverride("uppercase", on); });

    auto colorRow = [&](const QString &label, const QString &key) {
        auto *b = new ColorButton;
        f->addRow(label, b);
        connect(b, &ColorButton::colorChanged, this, [this, key](const QColor &c) { applyStyleOverride(key, c.name(QColor::HexArgb)); });
        return b;
    };
    auto *textColor = colorRow("Text colour", "textColor");
    auto *activeColor = colorRow("Highlight colour", "activeColor");
    auto *outlineColor = colorRow("Outline colour", "outlineColor");
    auto *outlineW = new SliderSpin(0, 0.4, 0.005, 3);
    f->addRow("Outline width", outlineW);
    connect(outlineW, &SliderSpin::valueChanged, this, [this](double v) { applyStyleOverride("outlineWidthPct", v); });
    auto *shadowColor = colorRow("Shadow", "shadowColor");
    auto *box = new QComboBox;
    for (const auto &[l, k] : QVector<QPair<QString, QString>>{{"None", "none"}, {"Behind each line", "line"}, {"Behind caption", "cue"},
                                                                {"Behind spoken word", "activeWord"}, {"Full-width bar", "bar"}})
        box->addItem(l, k);
    f->addRow("Background", box);
    connect(box, &QComboBox::currentIndexChanged, this, [this, box](int) { applyStyleOverride("boxMode", box->currentData().toString()); });
    auto *boxColor = colorRow("Background colour", "boxColor");

    auto *effect = new QComboBox;
    for (const auto &[l, k] : QVector<QPair<QString, QString>>{{"None", "none"}, {"Highlight spoken word", "highlight"},
                                                                {"Karaoke fill", "karaoke"}, {"Pop spoken word", "pop"},
                                                                {"One word at a time", "activeOnly"}, {"Reveal words", "reveal"},
                                                                {"Typewriter", "typewriter"}})
        effect->addItem(l, k);
    f->addRow("Word effect", effect);
    connect(effect, &QComboBox::currentIndexChanged, this, [this, effect](int) { applyStyleOverride("wordEffect", effect->currentData().toString()); });
    auto *entry = new QComboBox;
    for (const auto &[l, k] : QVector<QPair<QString, QString>>{{"None", "none"}, {"Fade", "fade"}, {"Pop", "pop"}, {"Slide up", "slideUp"}})
        entry->addItem(l, k);
    f->addRow("Entry", entry);
    connect(entry, &QComboBox::currentIndexChanged, this, [this, entry](int) { applyStyleOverride("entry", entry->currentData().toString()); });
    auto *exitC = new QComboBox;
    exitC->addItem("None", "none");
    exitC->addItem("Fade", "fade");
    f->addRow("Exit", exitC);
    connect(exitC, &QComboBox::currentIndexChanged, this, [this, exitC](int) { applyStyleOverride("exit", exitC->currentData().toString()); });
    auto *anim = new QSpinBox;
    anim->setRange(0, 2000);
    anim->setSingleStep(20);
    anim->setSuffix(" ms");
    anim->setKeyboardTracking(false);
    f->addRow("Animation time", anim);
    connect(anim, &QSpinBox::valueChanged, this, [this](int v) { applyStyleOverride("animMs", v); });

    auto *anchor = new QComboBox;
    anchor->addItem("Bottom (safe)", "bottom");
    anchor->addItem("Middle", "middle");
    anchor->addItem("Top (safe)", "top");
    f->addRow("Position", anchor);
    connect(anchor, &QComboBox::currentIndexChanged, this, [this, anchor](int) { applyStyleOverride("anchor", anchor->currentData().toString()); });
    auto *offset = new SliderSpin(-0.4, 0.4, 0.005, 3);
    f->addRow("Vertical offset", offset);
    connect(offset, &SliderSpin::valueChanged, this, [this](double v) { applyStyleOverride("offsetYPct", v); });
    auto *align = new QComboBox;
    align->addItem("Center", "center");
    align->addItem("Left", "left");
    align->addItem("Right", "right");
    f->addRow("Align", align);
    connect(align, &QComboBox::currentIndexChanged, this, [this, align](int) { applyStyleOverride("align", align->currentData().toString()); });
    auto *wpl = new QSpinBox;
    wpl->setRange(1, 20);
    wpl->setKeyboardTracking(false);
    f->addRow("Words per line", wpl);
    connect(wpl, &QSpinBox::valueChanged, this, [this](int v) { applyStyleOverride("maxWordsPerLine", v); });
    auto *lines = new QSpinBox;
    lines->setRange(1, 4);
    lines->setKeyboardTracking(false);
    f->addRow("Lines", lines);
    connect(lines, &QSpinBox::valueChanged, this, [this](int v) { applyStyleOverride("maxLines", v); });
    auto *speakerLabel = new QCheckBox("Show speaker names");
    f->addRow("", speakerLabel);
    connect(speakerLabel, &QCheckBox::toggled, this, [this](bool on) { applyStyleOverride("showSpeakerLabel", on); });
    auto *speakerColor = new QCheckBox("Colour highlight by speaker");
    f->addRow("", speakerColor);
    connect(speakerColor, &QCheckBox::toggled, this, [this](bool on) { applyStyleOverride("speakerColors", on); });

    auto *btns = new QHBoxLayout;
    auto *save = new QPushButton("Save as brand preset…");
    auto *reset = new QPushButton("Reset to template");
    btns->addWidget(save);
    btns->addWidget(reset);
    f->addRow("", btns);
    connect(save, &QPushButton::clicked, this, [this, style]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Save brand preset", "Preset name:", QLineEdit::Normal, "My brand captions", &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        CaptionStyle s = style();
        s.id.clear();
        s.name = name.trimmed();
        s.family = "Brand presets";
        s.description = "Saved from project " + m_doc->project().name;
        QString err;
        if (!TemplateLibrary::instance().saveUserTemplate(s, &err))
            QMessageBox::warning(this, "Save preset", err);
        else {
            m_gallery->reload();
            emit statusMessage("Saved brand preset \"" + s.name + "\" to User_Templates");
        }
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        const Track *t = captionTrack();
        if (!t)
            return;
        const QString id = t->id;
        m_doc->apply("Reset caption style", [&](Project &p) {
            Track *tt = p.findTrack(id);
            tt->styleOverrides = QJsonObject();
            if (Transcript *tr = p.findTranscript(tt->transcriptId))
                TranscriptEdit::resegment(*tr, TemplateLibrary::instance().get(tt->styleId));
            return true;
        });
    });

    m_styleRefreshers << [=]() {
        const CaptionStyle s = style();
        font->setCurrentFont(QFont(s.fontFamilies.value(0)));
        weight->setCurrentIndex(std::max(0, weight->findData(s.fontWeight)));
        size->setValue(s.sizePct);
        upper->setChecked(s.uppercase);
        textColor->setColor(s.textColor);
        activeColor->setColor(s.activeColor);
        outlineColor->setColor(s.outlineColor);
        outlineW->setValue(s.outlineWidthPct);
        shadowColor->setColor(s.shadowColor);
        box->setCurrentIndex(std::max(0, box->findData(s.boxMode)));
        boxColor->setColor(s.boxColor);
        effect->setCurrentIndex(std::max(0, effect->findData(s.wordEffect)));
        entry->setCurrentIndex(std::max(0, entry->findData(s.entry)));
        exitC->setCurrentIndex(std::max(0, exitC->findData(s.exit)));
        anim->setValue(s.animMs);
        anchor->setCurrentIndex(std::max(0, anchor->findData(s.anchor)));
        offset->setValue(s.offsetYPct);
        align->setCurrentIndex(std::max(0, align->findData(s.align)));
        wpl->setValue(s.maxWordsPerLine);
        lines->setValue(s.maxLines);
        speakerLabel->setChecked(s.showSpeakerLabel);
        speakerColor->setChecked(s.speakerColors);
    };
}

void SubtitlePanel::refreshStyle()
{
    m_updating = true;
    const Track *t = captionTrack();
    m_styleForm->setEnabled(t != nullptr);
    if (m_doc->isOpen())
        m_gallery->setAspect(m_doc->project().canvas.width, m_doc->project().canvas.height);
    if (t)
        m_gallery->setCurrentTemplate(t->styleId);
    for (auto &r : m_styleRefreshers)
        r();
    m_updating = false;
}

void SubtitlePanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_refreshPending) {
        m_refreshPending = false;
        refreshAll();
    }
}

void SubtitlePanel::refreshAll()
{
    m_refreshPending = false;
    refreshSources();
    refreshTrackCombo();
    refreshTable();
    refreshStyle();
}

} // namespace ss
