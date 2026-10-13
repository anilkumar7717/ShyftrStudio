#include "QuickPanel.h"
#include "../captions/CaptionEngine.h"
#include "../captions/Transcription.h"
#include "../core/Settings.h"
#include "../media/ExportGraph.h"
#include "TemplateGallery.h"
#include "Widgets.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace ss {

static QLabel *stepTitle(int n, const QString &text)
{
    auto *l = new QLabel(QString("<span style='color:#5B8CFF; font-weight:800;'>%1</span>&nbsp;&nbsp;<b>%2</b>").arg(n).arg(text));
    l->setStyleSheet("font-size: 13pt; margin-top: 6px;");
    return l;
}

QuickPanel::QuickPanel(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(12, 8, 12, 12);
    scroll->setWidget(body);

    // 1 import
    v->addWidget(stepTitle(1, "Import"));
    auto *imp = new QPushButton("Import recording…");
    v->addWidget(imp);
    m_mediaInfo = new QLabel;
    m_mediaInfo->setObjectName("hint");
    m_mediaInfo->setWordWrap(true);
    v->addWidget(m_mediaInfo);

    // 2 captions
    v->addWidget(stepTitle(2, "Auto-caption"));
    auto *row = new QHBoxLayout;
    m_language = new QComboBox;
    m_language->setMinimumContentsLength(8);
    m_language->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    for (const LanguageOption &l : Languages::all())
        m_language->addItem(l.label, l.code);
    m_language->setCurrentIndex(std::max(0, m_language->findData(Settings::instance().defaultLanguage)));
    m_generate = new QPushButton("Generate");
    m_generate->setObjectName("primary");
    row->addWidget(m_language, 1);
    row->addWidget(m_generate);
    v->addLayout(row);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->hide();
    v->addWidget(m_progress);
    m_genStatus = new QLabel("Runs locally — nothing is uploaded.");
    m_genStatus->setObjectName("hint");
    m_genStatus->setWordWrap(true);
    v->addWidget(m_genStatus);
    auto *editCaps = new QPushButton("Edit captions…");
    editCaps->setToolTip("Fix words, timing and line breaks (or double-click a caption on the video)");
    v->addWidget(editCaps);
    connect(editCaps, &QPushButton::clicked, this, &QuickPanel::editCaptionsRequested);

    // 3 style
    v->addWidget(stepTitle(3, "Pick a caption style"));
    m_gallery = new TemplateGallery;
    m_gallery->setCompact(true);
    m_gallery->setMinimumHeight(260);
    v->addWidget(m_gallery);

    // 4 music
    v->addWidget(stepTitle(4, "Add music"));
    auto *music = new QPushButton("Add background music…");
    music->setToolTip("Placed on its own track at −14 dB with fades, trimmed to your video");
    v->addWidget(music);
    auto *tts = new QPushButton("Voice-over from text…");
    tts->setToolTip("Text to speech with the voices on this computer");
    v->addWidget(tts);
    connect(tts, &QPushButton::clicked, this, [this]() { emit toolRequested("tts"); });

    // creative extras (open the Library on the left)
    {
        auto *pop = new QLabel("Make it pop");
        pop->setStyleSheet("font-weight: 700; margin-top: 6px;");
        v->addWidget(pop);
    }
    {
        QList<QWidget *> extras;
        for (const auto &[label, cat] : QVector<QPair<QString, QString>>{{"Text", "Text"}, {"Stickers", "Stickers"}, {"Effects", "Effects"},
                                                                          {"Transitions", "Transitions"}, {"Filters", "Filters"}}) {
            auto *b = new QToolButton;
            b->setText(label);
            b->setMinimumWidth(64);
            extras << b;
            const QString c = cat;
            connect(b, &QToolButton::clicked, this, [this, c]() { emit libraryRequested(c); });
        }
        auto *silence = new QToolButton;
        silence->setText("Cut silences");
        silence->setToolTip("Removes the quiet parts automatically");
        connect(silence, &QToolButton::clicked, this, [this]() { emit toolRequested("silences"); });
        extras << silence;
        v->addWidget(flowRow(extras));
    }

    // 5 format + look
    v->addWidget(stepTitle(5, "Format & look"));
    QList<QWidget *> fmtButtons;
    for (const auto &[label, size] : QVector<QPair<QString, QSize>>{{"16:9", {1920, 1080}}, {"9:16", {1080, 1920}}, {"1:1", {1080, 1080}}, {"4:5", {1080, 1350}}}) {
        auto *b = new QToolButton;
        b->setText(label);
        b->setMinimumWidth(52);
        fmtButtons << b;
        const QSize s = size;
        const QString name = label;
        connect(b, &QToolButton::clicked, this, [this, s, name]() {
            m_doc->apply("Change format to " + name, [&](Project &p) {
                p.canvas.width = s.width();
                p.canvas.height = s.height();
                for (auto &t : p.tracks)
                    if (t.kind == TrackKind::Video)
                        for (auto &c : t.clips)
                            if (c.type == ClipType::Media)
                                c.transform.fit = "fill";
                return true;
            });
        });
    }
    v->addWidget(flowRow(fmtButtons));
    m_look = new QComboBox;
    const QVector<QPair<QString, ColorAdjust>> looks{
        {"Look: Natural", ColorAdjust()},
        {"Look: Warm", {0.05, 1.05, 1.1, 0.45, 0.05}},
        {"Look: Cool", {0, 1.05, 0.95, -0.45, 0}},
        {"Look: Vivid", {0.05, 1.15, 1.45, 0.05, 0}},
        {"Look: Black & white", {0, 1.15, 0, 0, 0}},
        {"Look: Faded film", {0.15, 0.8, 0.7, 0.15, 0}},
    };
    for (int i = 0; i < looks.size(); ++i)
        m_look->addItem(looks[i].first, i);
    v->addWidget(m_look);
    connect(m_look, &QComboBox::activated, this, [this, looks](int i) {
        const ColorAdjust c = looks[i].second;
        m_doc->apply("Apply look", [&](Project &p) {
            for (auto &t : p.tracks)
                if (t.kind == TrackKind::Video)
                    for (auto &cl : t.clips)
                        if (cl.type == ClipType::Media)
                            cl.color = c;
            return true;
        });
    });

    // 6 export
    v->addWidget(stepTitle(6, "Export"));
    m_preset = new QComboBox;
    m_preset->addItem("Match project", "project");
    for (const ExportPreset &p : exportPresets())
        m_preset->addItem(p.label, p.id);
    v->addWidget(m_preset);
    auto *exp = new QPushButton("Export video…");
    exp->setObjectName("primary");
    v->addWidget(exp);
    v->addStretch(1);

    connect(imp, &QPushButton::clicked, this, &QuickPanel::importRequested);
    connect(m_generate, &QPushButton::clicked, this, [this]() {
        QString media;
        // prefer the media of the first audio clip on the timeline, else any media with audio
        for (const auto &t : m_doc->project().tracks)
            if (media.isEmpty() && t.kind == TrackKind::Audio && !t.clips.isEmpty())
                media = t.clips.first().mediaId;
        if (media.isEmpty())
            for (const MediaAsset &m : m_doc->project().media)
                if (m.hasAudio) {
                    media = m.id;
                    break;
                }
        if (media.isEmpty()) {
            emit statusMessage("Import a recording with speech first");
            return;
        }
        emit generateRequested(media, m_language->currentData().toString());
    });
    connect(music, &QPushButton::clicked, this, &QuickPanel::addMusicRequested);
    connect(exp, &QPushButton::clicked, this, [this]() { emit exportRequested(m_preset->currentData().toString()); });
    connect(m_gallery, &TemplateGallery::templateChosen, this, [this](const QString &id) {
        QString trackId;
        for (const auto &t : m_doc->project().tracks)
            if (t.kind == TrackKind::Caption) {
                trackId = t.id;
                break;
            }
        if (trackId.isEmpty()) {
            emit statusMessage("Generate captions first, then pick a style");
            return;
        }
        m_doc->apply("Apply caption template", [&](Project &p) {
            Track *t = p.findTrack(trackId);
            t->styleId = id;
            if (Transcript *tr = p.findTranscript(t->transcriptId))
                TranscriptEdit::resegment(*tr, TemplateLibrary::instance().get(id).withOverrides(t->styleOverrides));
            return true;
        });
    });
    connect(doc, &Document::projectChanged, this, &QuickPanel::refresh);
    connect(doc, &Document::projectReset, this, &QuickPanel::refresh);
    refresh();
}

void QuickPanel::refresh()
{
    if (!m_doc->isOpen())
        return;
    const Project &p = m_doc->project();
    m_mediaInfo->setText(p.media.isEmpty() ? "Drop a file onto the viewer, or click Import."
                                           : QString("%1 media file(s) · timeline %2").arg(p.media.size()).arg(formatClock(p.duration(), false)));
    m_gallery->setAspect(p.canvas.width, p.canvas.height);
    for (const auto &t : p.tracks)
        if (t.kind == TrackKind::Caption) {
            m_gallery->setCurrentTemplate(t.styleId);
            break;
        }
}

void QuickPanel::setGenerationState(bool running, int percent, const QString &status)
{
    m_generate->setEnabled(!running);
    m_progress->setVisible(running);
    m_progress->setValue(percent);
    m_genStatus->setText(status);
}

} // namespace ss
