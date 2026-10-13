#include "ToolsController.h"
#include "../core/ProjectStore.h"
#include "../core/TimelineOps.h"
#include "../media/Compositor.h"
#include "../media/FrameReader.h"
#include "../media/MediaProbe.h"
#include "PreviewEngine.h"
#include "Widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFuture>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <atomic>
#include <memory>

namespace ss {

ToolsController::ToolsController(Document *doc, PreviewEngine *engine, QWidget *window)
    : QObject(window), m_doc(doc), m_engine(engine), m_window(window)
{
}

QString ToolsController::generatedDir() const
{
    const QString d = m_doc->projectDir() + "/media/generated";
    QDir().mkpath(d);
    return d;
}

QString ToolsController::uniquePath(const QString &stem, const QString &ext) const
{
    QString base = stem;
    base.replace(QRegularExpression("[^A-Za-z0-9 _.-]"), "_");
    base = base.trimmed().left(48);
    if (base.isEmpty())
        base = "clip";
    QString path = generatedDir() + "/" + base + "." + ext;
    for (int i = 2; QFileInfo::exists(path); ++i)
        path = generatedDir() + QString("/%1-%2.%3").arg(base).arg(i).arg(ext);
    return path;
}

bool ToolsController::runWithProgress(const QString &title, const std::function<LocalTools::Result(const LocalTools::Progress &)> &fn,
                                      LocalTools::Result *out)
{
    m_engine->pause();
    auto progress = std::make_shared<std::atomic<int>>(-1);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    QProgressDialog dlg(title, "Cancel", 0, 1000, m_window);
    dlg.setWindowTitle("Shyftr Studio");
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(300);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.setValue(0);
    QFuture<LocalTools::Result> fut = QtConcurrent::run([fn, progress, cancel]() {
        return fn([progress, cancel](double f) {
            if (f >= 0)
                progress->store(int(f * 1000));
            return !cancel->load();
        });
    });
    while (!fut.isFinished()) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
        const int v = progress->load();
        if (v < 0) {
            dlg.setMaximum(0); // busy indicator
        } else {
            dlg.setMaximum(1000);
            dlg.setValue(v);
        }
        if (dlg.wasCanceled())
            cancel->store(true);
    }
    dlg.close();
    *out = fut.result();
    if (cancel->load()) {
        emit statusMessage("Cancelled");
        return false;
    }
    if (!out->ok) {
        QMessageBox::warning(m_window, "Shyftr Studio", out->error.isEmpty() ? QString("The operation failed.") : out->error);
        return false;
    }
    return true;
}

static bool probeFile(const QString &path, MediaAsset *m, QString *err)
{
    if (!MediaProbe::probe(path, *m, err))
        return false;
    m->id = newId();
    return true;
}

QStringList ToolsController::targetClips(bool needAudio, bool needVideo) const
{
    const Project &p = m_doc->project();
    QStringList out;
    for (const QString &id : m_doc->selectedClips()) {
        const Track *t = nullptr;
        const Clip *c = p.findClip(id, &t);
        if (!c || c->type != ClipType::Media)
            continue;
        const MediaAsset *m = p.findMedia(c->mediaId);
        if (!m || m->missing)
            continue;
        if (needAudio && !m->hasAudio)
            continue;
        if (needVideo && (t->kind != TrackKind::Video || m->kind != MediaKind::Video))
            continue;
        // one clip per linked pair (prefer the video side)
        bool dup = false;
        for (const QString &o : out)
            if (const Clip *oc = p.findClip(o); oc && !oc->linkId.isEmpty() && oc->linkId == c->linkId)
                dup = true;
        if (!dup)
            out << id;
    }
    return out;
}

// ------------------------------------------------------------------ media replacing tools

void ToolsController::replaceClipMedia(const QString &clipId, const QString &newFile, const QString &what)
{
    MediaAsset m;
    QString err;
    if (!probeFile(newFile, &m, &err)) {
        QMessageBox::warning(m_window, "Shyftr Studio", "The new file could not be read:\n" + err);
        return;
    }
    m.relativePath = ProjectStore::relativeTo(m_doc->projectDir(), m.path);
    m_doc->apply(what, [&](Project &p) {
        p.media << m;
        bool any = false;
        for (Clip *c : p.linkedClips(clipId)) {
            const TimeUs len = std::min(c->out - c->in, m.duration > 0 ? m.duration : c->out - c->in);
            c->mediaId = m.id;
            c->in = 0;
            c->out = std::max<TimeUs>(frameDurationUs(p.canvas.fps), len);
            any = true;
        }
        return any;
    });
}

void ToolsController::reverseClip(const QString &clipId)
{
    const Clip *c = m_doc->project().findClip(clipId);
    const MediaAsset *m = c ? m_doc->project().findMedia(c->mediaId) : nullptr;
    if (!c || !m || m->kind != MediaKind::Video) {
        emit statusMessage("Select a video clip to reverse");
        return;
    }
    const QString src = m->path, dst = uniquePath(QFileInfo(m->name).completeBaseName() + "-reversed", "mp4");
    const TimeUs in = c->in, out = c->out;
    const bool audio = m->hasAudio;
    LocalTools::Result r;
    if (runWithProgress("Reversing the clip on this computer…", [=](const LocalTools::Progress &pr) {
            return LocalTools::reverse(src, in, out, audio, dst, pr);
        }, &r)) {
        replaceClipMedia(clipId, r.path, "Reverse clip");
        emit statusMessage("Clip reversed (undo with ⌘Z / Ctrl+Z)");
    }
}

void ToolsController::stabilizeClip(const QString &clipId)
{
    const Clip *c = m_doc->project().findClip(clipId);
    const MediaAsset *m = c ? m_doc->project().findMedia(c->mediaId) : nullptr;
    if (!c || !m || m->kind != MediaKind::Video) {
        emit statusMessage("Select a video clip to stabilise");
        return;
    }
    const QString src = m->path, dst = uniquePath(QFileInfo(m->name).completeBaseName() + "-stable", "mp4");
    const TimeUs in = c->in, out = c->out;
    const bool audio = m->hasAudio;
    LocalTools::Result r;
    if (runWithProgress("Stabilising the clip on this computer…", [=](const LocalTools::Progress &pr) {
            return LocalTools::stabilize(src, in, out, audio, dst, pr);
        }, &r)) {
        replaceClipMedia(clipId, r.path, "Stabilise clip");
        emit statusMessage("Clip stabilised");
    }
}

void ToolsController::freezeFrame(const QString &clipIdIn)
{
    const Project &p = m_doc->project();
    const TimeUs t = m_doc->playhead();
    QString clipId = clipIdIn;
    if (clipId.isEmpty()) {
        const auto active = TimelineOps::activeVideoClips(p, t);
        if (!active.isEmpty())
            clipId = active.last()->id;
    }
    const Clip *c = p.findClip(clipId);
    const MediaAsset *m = c ? p.findMedia(c->mediaId) : nullptr;
    if (!c || !m || m->kind != MediaKind::Video) {
        emit statusMessage("Put the playhead over a video clip to freeze a frame");
        return;
    }
    const TimeUs at = c->containsTimeline(t) ? t : c->start;
    const TimeUs src = c->timelineToSource(at);
    const QString png = uniquePath(QFileInfo(m->name).completeBaseName() + "-freeze", "png");
    const QString path = m->path;
    LocalTools::Result r;
    if (!runWithProgress("Capturing the frame…", [=](const LocalTools::Progress &) { return LocalTools::extractFrame(path, src, png); }, &r))
        return;
    MediaAsset img;
    QString err;
    if (!probeFile(r.path, &img, &err)) {
        QMessageBox::warning(m_window, "Shyftr Studio", err);
        return;
    }
    img.relativePath = ProjectStore::relativeTo(m_doc->projectDir(), img.path);
    QString newClip;
    const bool ok = m_doc->apply("Freeze frame", [&](Project &pr) {
        pr.media << img;
        newClip = TimelineOps::insertFreezeFrame(pr, clipId, at, img.id, 3 * kUsPerSec);
        return !newClip.isEmpty();
    });
    if (!ok) {
        QFile::remove(r.path);
        emit statusMessage("The freeze frame could not be inserted here (is a track locked?)");
        return;
    }
    {
        m_doc->setSelectedClips({newClip});
        emit statusMessage("3-second freeze frame inserted — trim it on the timeline to change its length");
    }
}

void ToolsController::extractAudio(const QString &clipId)
{
    const Clip *c = m_doc->project().findClip(clipId);
    const MediaAsset *m = c ? m_doc->project().findMedia(c->mediaId) : nullptr;
    if (!c || !m || !m->hasAudio) {
        emit statusMessage("This clip has no sound to extract");
        return;
    }
    const QString path = m->path, dst = uniquePath(QFileInfo(m->name).completeBaseName() + "-audio", "m4a");
    const TimeUs in = c->in, out = c->out, start = c->start;
    const double speed = c->speed;
    LocalTools::Result r;
    if (!runWithProgress("Extracting audio…", [=](const LocalTools::Progress &) { return LocalTools::extractAudio(path, in, out, dst); }, &r))
        return;
    MediaAsset a;
    QString err;
    if (!probeFile(r.path, &a, &err))
        return (void)QMessageBox::warning(m_window, "Shyftr Studio", err);
    a.relativePath = ProjectStore::relativeTo(m_doc->projectDir(), a.path);
    m_doc->apply("Extract audio", [&](Project &p) {
        p.media << a;
        // mute the clip's own sound so it is not heard twice
        for (Clip *l : p.linkedClips(clipId)) {
            const Track *t = nullptr;
            p.findClip(l->id, &t);
            if (t && t->kind == TrackKind::Audio)
                l->audio.mute = true;
        }
        QString track;
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Audio && !t.locked && TimelineOps::isRangeFree(t, start, TimeUs(double(out - in) / speed)))
                track = t.id;
        if (track.isEmpty())
            track = TimelineOps::addTrack(p, TrackKind::Audio, "Extracted");
        const QString id = TimelineOps::addMediaClip(p, a.id, track, start, false);
        if (Clip *nc = p.findClip(id)) {
            nc->speed = speed;
            nc->name = "Extracted audio";
        }
        return !id.isEmpty();
    });
    emit statusMessage("Audio extracted to its own track (and saved in the project's media/generated folder)");
}

// ------------------------------------------------------------------ smart cuts

void ToolsController::removeSilences()
{
    QStringList targets = targetClips(true, false);
    const Project &p = m_doc->project();
    if (targets.isEmpty()) {
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Video && !t.locked)
                for (const Clip &c : t.clips)
                    if (const MediaAsset *m = p.findMedia(c.mediaId); c.type == ClipType::Media && m && m->hasAudio && !m->missing)
                        targets << c.id;
    }
    if (targets.isEmpty()) {
        emit statusMessage("No clips with sound to process");
        return;
    }
    QDialog dlg(m_window);
    dlg.setWindowTitle("Remove silences");
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QString("Cuts out quiet parts from %1 clip(s) and closes the gaps. Captions stay in sync.").arg(targets.size())));
    auto *f = new QFormLayout;
    auto *thr = new QDoubleSpinBox;
    thr->setRange(-70, -10);
    thr->setValue(-35);
    thr->setSuffix(" dB");
    thr->setToolTip("Anything quieter than this counts as silence");
    auto *minDur = new QDoubleSpinBox;
    minDur->setRange(0.2, 5);
    minDur->setSingleStep(0.1);
    minDur->setValue(0.6);
    minDur->setSuffix(" s");
    auto *pad = new QDoubleSpinBox;
    pad->setRange(0, 0.5);
    pad->setSingleStep(0.02);
    pad->setValue(0.12);
    pad->setSuffix(" s");
    pad->setToolTip("Breathing room kept before and after speech");
    f->addRow("Silence level", thr);
    f->addRow("Shortest pause to cut", minDur);
    f->addRow("Keep around speech", pad);
    v->addLayout(f);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Remove silences");
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    struct Job {
        QString clip, path;
        TimeUs in, out;
    };
    QVector<Job> jobs;
    for (const QString &id : targets) {
        const Clip *c = p.findClip(id);
        jobs << Job{id, p.findMedia(c->mediaId)->path, c->in, c->out};
    }
    const double th = thr->value();
    const TimeUs md = TimeUs(minDur->value() * kUsPerSec), pd = TimeUs(pad->value() * kUsPerSec);
    auto results = std::make_shared<QHash<QString, QVector<QPair<TimeUs, TimeUs>>>>();
    LocalTools::Result r;
    if (!runWithProgress("Finding silences…", [=](const LocalTools::Progress &pr) {
            LocalTools::Result res;
            for (int i = 0; i < jobs.size(); ++i) {
                if (!pr(double(i) / jobs.size()))
                    return res;
                QString err;
                (*results)[jobs[i].clip] = LocalTools::silences(jobs[i].path, jobs[i].in, jobs[i].out, th, md, pd, &err);
                if (!err.isEmpty()) {
                    res.error = err;
                    return res;
                }
            }
            res.ok = true;
            return res;
        }, &r))
        return;
    int total = 0;
    m_doc->apply("Remove silences", [&](Project &pr) {
        for (auto it = results->begin(); it != results->end(); ++it)
            total += TimelineOps::removeSourceRanges(pr, it.key(), it.value());
        return total > 0;
    });
    emit statusMessage(total ? QString("Removed %1 silent part(s)").arg(total) : QString("No silences long enough were found"));
}

void ToolsController::removeFillerWords()
{
    const Project &p = m_doc->project();
    QStringList targets = targetClips(true, false);
    if (targets.isEmpty())
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Video && !t.locked)
                for (const Clip &c : t.clips)
                    if (c.type == ClipType::Media)
                        targets << c.id;
    QHash<QString, QVector<QPair<TimeUs, TimeUs>>> ranges;
    int words = 0;
    bool anyTranscript = false;
    for (const QString &id : targets) {
        const Clip *c = p.findClip(id);
        for (const Transcript &tr : p.transcripts)
            if (tr.mediaId == c->mediaId) {
                anyTranscript = true;
                auto r = TimelineOps::fillerWordRanges(tr, TimelineOps::defaultFillerWords());
                QVector<QPair<TimeUs, TimeUs>> inside;
                for (const auto &x : r)
                    if (x.second > c->in && x.first < c->out)
                        inside << x;
                words += inside.size();
                ranges[id] += inside;
            }
    }
    if (!anyTranscript) {
        QMessageBox::information(m_window, "Remove filler words",
                                 "Filler words are found from the captions. Generate captions for this video first "
                                 "(AI Subtitles → Generate), then run this again.");
        return;
    }
    if (words == 0) {
        emit statusMessage("No filler words (um, uh, erm…) were found");
        return;
    }
    if (QMessageBox::question(m_window, "Remove filler words",
                              QString("Found %1 filler word(s) such as “um” and “uh”. Cut them out and close the gaps?").arg(words))
        != QMessageBox::Yes)
        return;
    int removed = 0;
    m_doc->apply("Remove filler words", [&](Project &pr) {
        for (auto it = ranges.begin(); it != ranges.end(); ++it)
            removed += TimelineOps::removeSourceRanges(pr, it.key(), it.value());
        return removed > 0;
    });
    emit statusMessage(QString("Removed %1 filler word(s)").arg(removed));
}

void ToolsController::splitScenes()
{
    const Project &p = m_doc->project();
    QStringList targets = targetClips(false, true);
    if (targets.isEmpty()) {
        emit statusMessage("Select one or more video clips to split by scene");
        return;
    }
    struct Job {
        QString clip, path;
        TimeUs in, out;
    };
    QVector<Job> jobs;
    for (const QString &id : targets) {
        const Clip *c = p.findClip(id);
        jobs << Job{id, p.findMedia(c->mediaId)->path, c->in, c->out};
    }
    auto results = std::make_shared<QHash<QString, QVector<TimeUs>>>();
    LocalTools::Result r;
    if (!runWithProgress("Detecting scene changes…", [=](const LocalTools::Progress &pr) {
            LocalTools::Result res;
            for (int i = 0; i < jobs.size(); ++i) {
                if (!pr(double(i) / jobs.size()))
                    return res;
                QString err;
                (*results)[jobs[i].clip] = LocalTools::scenes(jobs[i].path, jobs[i].in, jobs[i].out, 0.32, &err);
                if (!err.isEmpty()) {
                    res.error = err;
                    return res;
                }
            }
            res.ok = true;
            return res;
        }, &r))
        return;
    int cuts = 0;
    m_doc->apply("Split by scenes", [&](Project &pr) {
        for (auto it = results->begin(); it != results->end(); ++it) {
            QVector<TimeUs> times;
            if (const Clip *c = pr.findClip(it.key()))
                for (TimeUs s : it.value())
                    times << c->sourceToTimeline(s);
            std::sort(times.begin(), times.end(), std::greater<TimeUs>());
            for (TimeUs t : times)
                if (TimelineOps::splitClip(pr, it.key(), t, true))
                    ++cuts;
        }
        return cuts > 0;
    });
    emit statusMessage(cuts ? QString("Split into scenes: %1 cut(s)").arg(cuts) : QString("No scene changes were found"));
}

void ToolsController::detectBeats()
{
    const Project &p = m_doc->project();
    QString clipId;
    for (const QString &id : m_doc->selectedClips()) {
        const Clip *c = p.findClip(id);
        const MediaAsset *m = c ? p.findMedia(c->mediaId) : nullptr;
        if (m && m->hasAudio)
            clipId = id;
    }
    if (clipId.isEmpty()) // otherwise the first music clip
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Audio && clipId.isEmpty())
                for (const Clip &c : t.clips)
                    if (const MediaAsset *m = p.findMedia(c.mediaId); m && m->kind == MediaKind::Audio) {
                        clipId = c.id;
                        break;
                    }
    const Clip *c = p.findClip(clipId);
    if (!c) {
        emit statusMessage("Select a music clip to find its beats");
        return;
    }
    const QString path = p.findMedia(c->mediaId)->path;
    const TimeUs in = c->in, out = c->out;
    auto beats = std::make_shared<QVector<TimeUs>>();
    LocalTools::Result r;
    if (!runWithProgress("Finding the beats…", [=](const LocalTools::Progress &) {
            LocalTools::Result res;
            QString err;
            *beats = LocalTools::beats(path, in, out, 0.55, &err);
            res.ok = err.isEmpty();
            res.error = err;
            return res;
        }, &r))
        return;
    const Clip clip = *c;
    m_doc->apply("Beat markers", [&](Project &pr) {
        for (TimeUs s : *beats) {
            const TimeUs t = snapToFrame(clip.sourceToTimeline(s), pr.canvas.fps);
            bool exists = false;
            for (const Marker &m : pr.markers)
                exists = exists || std::llabs(m.t - t) < frameDurationUs(pr.canvas.fps);
            if (!exists)
                pr.markers << Marker{t, QString(), QColor("#FF6BD6")};
        }
        std::sort(pr.markers.begin(), pr.markers.end(), [](const Marker &a, const Marker &b) { return a.t < b.t; });
        return !beats->isEmpty();
    });
    emit statusMessage(QString("Added %1 beat markers — clips snap to them while you drag").arg(beats->size()));
}

// ------------------------------------------------------------------ voice

void ToolsController::textToSpeech()
{
    if (LocalTools::speechEngineName().isEmpty()) {
        QMessageBox::information(m_window, "Text to speech",
                                 "No speech engine was found on this computer. On Linux install espeak-ng; macOS and Windows have voices built in.");
        return;
    }
    QDialog dlg(m_window);
    dlg.setWindowTitle("Text to speech");
    dlg.resize(520, 420);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel("Turns text into a voice-over using the voices installed on this computer (" + LocalTools::speechEngineName() + ")."));
    auto *text = new QPlainTextEdit;
    text->setPlaceholderText("Type what the voice should say…");
    // prefill with a selected title's text
    for (const QString &id : m_doc->selectedClips())
        if (const Clip *c = m_doc->project().findClip(id); c && c->type == ClipType::Title)
            text->setPlainText(c->title.text);
    v->addWidget(text, 1);
    auto *fromCaptions = new QPushButton("Use the caption text");
    v->addWidget(fromCaptions);
    connect(fromCaptions, &QPushButton::clicked, text, [this, text]() {
        QStringList lines;
        for (const Transcript &t : m_doc->project().transcripts)
            for (int i = 0; i < t.cues.size(); ++i)
                lines << t.cueText(i);
        text->setPlainText(lines.join(' '));
    });
    auto *f = new QFormLayout;
    auto *voice = new QComboBox;
    voice->addItem("Default voice", "");
    for (const QString &vo : LocalTools::voices())
        voice->addItem(vo, vo);
    // prefer an Indian English / Hindi voice when present
    for (int i = 0; i < voice->count(); ++i)
        if (voice->itemText(i).contains("en_IN") || voice->itemText(i).contains("en-IN")) {
            voice->setCurrentIndex(i);
            break;
        }
    auto *rate = new QDoubleSpinBox;
    rate->setRange(0.5, 2.0);
    rate->setSingleStep(0.05);
    rate->setValue(1.0);
    rate->setSuffix("×");
    f->addRow("Voice", voice);
    f->addRow("Speed", rate);
    v->addLayout(f);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Add voice-over at playhead");
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || text->toPlainText().trimmed().isEmpty())
        return;
    const QString txt = text->toPlainText(), vo = voice->currentData().toString();
    const double rt = rate->value();
    const QString dst = uniquePath("voiceover", "wav");
    LocalTools::Result r;
    if (!runWithProgress("Creating the voice-over…", [=](const LocalTools::Progress &) { return LocalTools::textToSpeech(txt, vo, rt, dst); }, &r))
        return;
    MediaAsset a;
    QString err;
    if (!probeFile(r.path, &a, &err))
        return (void)QMessageBox::warning(m_window, "Shyftr Studio", err);
    a.name = "Voice-over: " + txt.simplified().left(30);
    a.relativePath = ProjectStore::relativeTo(m_doc->projectDir(), a.path);
    const TimeUs at = m_doc->playhead();
    m_doc->apply("Text to speech", [&](Project &p) {
        p.media << a;
        QString track;
        for (const Track &t : p.tracks)
            if (t.kind == TrackKind::Audio && !t.locked && TimelineOps::isRangeFree(t, at, a.duration)) {
                track = t.id;
                break;
            }
        if (track.isEmpty())
            track = TimelineOps::addTrack(p, TrackKind::Audio, "Voice-over");
        return !TimelineOps::addMediaClip(p, a.id, track, at, false).isEmpty();
    });
    emit statusMessage("Voice-over added at the playhead");
}

// ------------------------------------------------------------------ snapshot & markers

void ToolsController::snapshot()
{
    const Project &p = m_doc->project();
    const QString def = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" + p.name + " "
                        + formatClock(m_doc->playhead(), false).replace(':', '.') + ".png";
    const QString path = QFileDialog::getSaveFileName(m_window, "Save frame as image", def, "PNG image (*.png);;JPEG image (*.jpg)");
    if (path.isEmpty())
        return;
    m_engine->pause();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    FrameReader reader(p, QSize(p.canvas.width, p.canvas.height));
    QImage img(p.canvas.width, p.canvas.height, QImage::Format_ARGB32_Premultiplied);
    Compositor::render(img, p, OverlayScene::build(p), m_doc->playhead(), reader);
    QApplication::restoreOverrideCursor();
    if (img.convertToFormat(QImage::Format_RGB32).save(path))
        emit statusMessage("Frame saved: " + QDir::toNativeSeparators(path));
    else
        QMessageBox::warning(m_window, "Shyftr Studio", "The image could not be saved to " + path);
}

void ToolsController::addMarker()
{
    const TimeUs t = snapToFrame(m_doc->playhead(), m_doc->project().canvas.fps);
    m_doc->apply("Marker", [t](Project &p) {
        for (int i = 0; i < p.markers.size(); ++i)
            if (std::llabs(p.markers[i].t - t) < frameDurationUs(p.canvas.fps)) {
                p.markers.removeAt(i); // toggle
                return true;
            }
        p.markers << Marker{t, QString(), QColor("#FFD43B")};
        std::sort(p.markers.begin(), p.markers.end(), [](const Marker &a, const Marker &b) { return a.t < b.t; });
        return true;
    });
}

void ToolsController::clearMarkers()
{
    m_doc->apply("Clear markers", [](Project &p) {
        if (p.markers.isEmpty())
            return false;
        p.markers.clear();
        return true;
    });
}

void ToolsController::jumpMarker(int dir)
{
    const TimeUs now = m_doc->playhead();
    const auto &mk = m_doc->project().markers;
    TimeUs target = -1;
    if (dir > 0) {
        for (const Marker &m : mk)
            if (m.t > now + 1000) {
                target = m.t;
                break;
            }
    } else {
        for (int i = mk.size() - 1; i >= 0; --i)
            if (mk[i].t < now - 1000) {
                target = mk[i].t;
                break;
            }
    }
    if (target >= 0)
        m_engine->seek(target);
}

} // namespace ss
