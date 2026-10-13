#include "MainWindow.h"
#include "../captions/CaptionEngine.h"
#include "../captions/CaptionStyle.h"
#include "../core/ProjectSerializer.h"
#include "../core/ProjectStore.h"
#include "../core/Settings.h"
#include "../core/TimelineOps.h"
#include "../core/Tools.h"
#include "../media/MediaCache.h"
#include "../media/MediaProbe.h"
#include "EffectsBrowser.h"
#include "ExportDialog.h"
#include "InspectorWidget.h"
#include "ToolsController.h"
#include "MediaBinWidget.h"
#include "PreferencesDialog.h"
#include "PreviewEngine.h"
#include "QuickPanel.h"
#include "RecordDialog.h"
#include "RelinkDialog.h"
#include "SubtitlePanel.h"
#include "Theme.h"
#include "TimelineWidget.h"
#include "ViewerWidget.h"
#include "WelcomePage.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <limits>
#include <algorithm>

namespace ss {

MainWindow::MainWindow()
{
    setWindowIcon(Theme::appIcon());
    setAcceptDrops(true);
    setDockNestingEnabled(true);
    m_cache = new MediaCache(this);
    m_engine = new PreviewEngine(&m_doc, this);
    m_doc.undoStack()->setUndoLimit(Settings::instance().undoLimit);
    buildUi();
    buildActions();
    buildMenus();
    applyShortcuts();
    showWelcome();
    setMinimumSize(900, 560);
    fitToScreen();

    connect(&m_autosave, &QTimer::timeout, this, &MainWindow::autosaveTick);
    connect(&m_doc, &Document::dirtyChanged, this, &MainWindow::updateTitle);
    connect(&m_doc, &Document::playheadChanged, this, [this](TimeUs) {
        if (m_engine->isPlaying())
            m_timeline->ensurePlayheadVisible(true);
    });
}

MainWindow::~MainWindow()
{
    if (m_lock)
        m_lock->unlock();
    delete m_lock;
}

// ================================================================== UI

void MainWindow::buildUi()
{
    m_stack = new QStackedWidget;
    m_welcome = new WelcomePage;
    m_stack->addWidget(m_welcome);
    m_viewer = new ViewerWidget(&m_doc, m_engine);
    m_stack->addWidget(m_viewer);
    setCentralWidget(m_stack);

    auto dock = [this](const QString &title, const QString &name, QWidget *w, Qt::DockWidgetArea area) {
        auto *d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        addDockWidget(area, d);
        return d;
    };
    m_timeline = new TimelineWidget(&m_doc, m_cache);
    m_mediaBin = new MediaBinWidget(&m_doc, m_cache);
    m_inspector = new InspectorWidget(&m_doc);
    m_subtitles = new SubtitlePanel(&m_doc);
    m_effects = new EffectsBrowser(&m_doc);
    m_quick = new QuickPanel(&m_doc);

    m_timelineDock = dock("Timeline", "timelineDock", m_timeline, Qt::BottomDockWidgetArea);
    m_mediaDock = dock("Media", "mediaDock", m_mediaBin, Qt::LeftDockWidgetArea);
    m_effectsDock = dock("Library", "effectsDock", m_effects, Qt::LeftDockWidgetArea);
    m_tools = new ToolsController(&m_doc, m_engine, this);
    m_inspectorDock = dock("Inspector", "inspectorDock", m_inspector, Qt::RightDockWidgetArea);
    m_subtitleDock = dock("AI Subtitles", "subtitleDock", m_subtitles, Qt::RightDockWidgetArea);
    m_quickDock = dock("Quick Editor", "quickDock", m_quick, Qt::RightDockWidgetArea);
    tabifyDockWidget(m_mediaDock, m_effectsDock);
    tabifyDockWidget(m_inspectorDock, m_subtitleDock);
    m_mediaDock->raise();
    m_inspectorDock->raise();
    for (QDockWidget *d : {m_mediaDock, m_effectsDock, m_inspectorDock, m_subtitleDock, m_quickDock})
        d->widget()->setMinimumWidth(200);

    m_saveState = new QLabel;
    m_offline = new QLabel("● Offline · all processing on this computer");
    m_offline->setStyleSheet("color: #69DB7C;");
    statusBar()->addPermanentWidget(m_saveState);
    statusBar()->addPermanentWidget(m_offline);

    // wiring
    connect(m_welcome, &WelcomePage::createRequested, this, &MainWindow::createProject);
    connect(m_welcome, &WelcomePage::openRequested, this, &MainWindow::openProject);
    connect(m_welcome, &WelcomePage::openDialogRequested, this, &MainWindow::openProjectDialog);
    connect(m_welcome, &WelcomePage::preferencesRequested, this, [this]() { openPreferences(false); });
    connect(m_viewer, &ViewerWidget::filesDropped, this, [this](const QStringList &p) { importFiles(p); });
    connect(m_timeline, &TimelineWidget::seekRequested, m_engine, &PreviewEngine::seek);
    connect(m_timeline, &TimelineWidget::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 3000); });
    connect(m_timeline, &TimelineWidget::clipActivated, this, [this](const QString &id) {
        m_inspector->focusClip(id);
        if (!m_quickMode) {
            m_inspectorDock->show();
            m_inspectorDock->raise();
        }
    });
    connect(m_timeline, &TimelineWidget::cueActivated, this, [this](const QString &trackId, int cue) {
        m_doc.setSelectedCue(trackId, cue);
        showSubtitleTab(1);
        // jump into the caption and edit it in place on the video
        if (const Track *t = m_doc.project().findTrack(trackId))
            for (const CueInstance &ci : CaptionMapper::map(m_doc.project(), *t))
                if (ci.cueIndex == cue) {
                    m_engine->seek(ci.start + (ci.end - ci.start) / 2);
                    break;
                }
        editCaptionInViewer();
    });
    connect(m_viewer, &ViewerWidget::captionEditRequested, this, [this](const QRect &anchor) { editCaptionInViewer(anchor); });
    connect(m_viewer, &ViewerWidget::titleEditRequested, this, &MainWindow::editTitleInViewer);
    connect(m_viewer, &ViewerWidget::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_viewer, &ViewerWidget::effectDropped, this, [this](const QString &id, const QPointF &pos) {
        m_effects->applyEffect(id, pos);
    });
    connect(m_timeline, &TimelineWidget::libraryItemDropped, this,
            [this](const QString &id, const QString &clip, TimeUs at, const QString &track) {
                m_effects->applyEffect(id, QPointF(-1, -1), clip, at, track);
            });
    connect(m_inspector, &InspectorWidget::reverseRequested, m_tools, &ToolsController::reverseClip);
    connect(m_inspector, &InspectorWidget::freezeFrameRequested, m_tools, &ToolsController::freezeFrame);
    connect(m_inspector, &InspectorWidget::stabilizeRequested, m_tools, &ToolsController::stabilizeClip);
    connect(m_inspector, &InspectorWidget::extractAudioRequested, m_tools, &ToolsController::extractAudio);
    connect(m_inspector, &InspectorWidget::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 5000); });
    connect(m_tools, &ToolsController::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 6000); });
    connect(m_subtitles, &SubtitlePanel::captionsReady, this, [this]() {
        showSubtitleTab(1);
        statusBar()->showMessage("Captions ready — click any caption to edit, or double-click it on the video", 8000);
    });
    connect(m_quick, &QuickPanel::editCaptionsRequested, this, [this]() { showSubtitleTab(1); });
    connect(m_quick, &QuickPanel::libraryRequested, this, [this](const QString &cat) {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory(cat);
    });
    connect(m_quick, &QuickPanel::toolRequested, this, [this](const QString &tool) {
        if (tool == "tts")
            m_tools->textToSpeech();
        else if (tool == "silences")
            m_tools->removeSilences();
    });
    connect(m_timeline, &TimelineWidget::filesDropped, this,
            [this](const QStringList &paths, TimeUs at, const QString &track, bool insert) {
                importFiles(paths, at, track, false, insert);
            });
    connect(m_mediaBin, &MediaBinWidget::importRequested, this, &MainWindow::importDialog);
    connect(m_mediaBin, &MediaBinWidget::recordRequested, this, &MainWindow::recordVoiceOver);
    connect(m_mediaBin, &MediaBinWidget::relinkRequested, this, &MainWindow::relinkMedia);
    connect(m_mediaBin, &MediaBinWidget::filesDropped, this, [this](const QStringList &p) { importFiles(p); });
    connect(m_mediaBin, &MediaBinWidget::addToTimelineRequested, this, [this](const QStringList &ids) {
        const TimeUs at = m_doc.playhead();
        m_doc.apply("Add to timeline", [&](Project &p) {
            TimeUs t = at;
            bool any = false;
            for (const QString &id : ids) {
                const QString cid = TimelineOps::addMediaClip(p, id, {}, t);
                if (!cid.isEmpty()) {
                    t = p.findClip(cid)->end();
                    any = true;
                }
            }
            return any;
        });
    });
    connect(m_subtitles, &SubtitlePanel::seekRequested, m_engine, &PreviewEngine::seek);
    connect(m_subtitles, &SubtitlePanel::manageModelsRequested, this, [this]() { openPreferences(true); });
    connect(m_subtitles, &SubtitlePanel::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 5000); });
    connect(m_subtitles, &SubtitlePanel::generationStateChanged, m_quick, &QuickPanel::setGenerationState);
    connect(m_subtitles, &SubtitlePanel::generationStateChanged, this, [this](bool running, int pct, const QString &status) {
        statusBar()->showMessage(running ? QString("%1 (%2%)").arg(status).arg(pct) : status, running ? 0 : 6000);
    });
    connect(m_effects, &EffectsBrowser::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 4000); });
    connect(m_quick, &QuickPanel::importRequested, this, &MainWindow::importDialog);
    connect(m_quick, &QuickPanel::generateRequested, m_subtitles, &SubtitlePanel::generate);
    connect(m_quick, &QuickPanel::addMusicRequested, this, &MainWindow::addMusic);
    connect(m_quick, &QuickPanel::exportRequested, this, &MainWindow::openExport);
    connect(m_quick, &QuickPanel::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 4000); });
    connect(m_engine, &PreviewEngine::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 4000); });
}

QAction *MainWindow::act(const QString &id, const QString &label, const QKeySequence &key, const std::function<void()> &fn,
                         bool editingCommand)
{
    auto *a = new QAction(label, this);
    a->setObjectName(id);
    Shortcuts::instance().registerAction(id, label, key);
    connect(a, &QAction::triggered, this, [this, fn, editingCommand]() {
        if (editingCommand && isTypingInText())
            return;
        fn();
    });
    addAction(a);
    m_actions.insert(id, a);
    return a;
}

bool MainWindow::isTypingInText() const
{
    QWidget *w = QApplication::focusWidget();
    if (!w)
        return false;
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QPlainTextEdit *>(w) ||
        qobject_cast<QTextEdit *>(w))
        return true;
    if (auto *view = qobject_cast<QAbstractItemView *>(w))
        return m_subtitles->isAncestorOf(view);
    return false;
}

void MainWindow::buildActions()
{
    Shortcuts::instance().load();
    // File
    act("file.new", "New Project…", QKeySequence::New, [this]() { if (closeProject()) showWelcome(); });
    act("file.open", "Open Project…", QKeySequence::Open, [this]() { openProjectDialog(); });
    act("file.save", "Save", QKeySequence::Save, [this]() { saveProject(); });
    act("file.package", "Save Portable Package…", QKeySequence(), [this]() { packageProject(); });
    act("file.collect", "Collect Media into Project", QKeySequence(), [this]() { collectMedia(); });
    act("file.relink", "Relink Media…", QKeySequence(), [this]() { relinkMedia(); });
    act("file.import", "Import Media…", QKeySequence("Ctrl+I"), [this]() { importDialog(); });
    act("file.importSubs", "Import Subtitles (SRT/VTT)…", QKeySequence(), [this]() {
        const QString p = QFileDialog::getOpenFileName(this, "Import subtitles", QString(), "Subtitles (*.srt *.vtt)");
        if (!p.isEmpty())
            m_subtitles->importSubtitles(p);
    });
    act("file.record", "Record Voice-over…", QKeySequence("Ctrl+Shift+R"), [this]() { recordVoiceOver(); });
    act("file.export", "Export Video…", QKeySequence("Ctrl+E"), [this]() { openExport(); });
    act("file.exportSrt", "Export Subtitles as SRT…", QKeySequence(), [this]() { m_subtitles->exportSubtitles("srt"); });
    act("file.exportVtt", "Export Subtitles as WebVTT…", QKeySequence(), [this]() { m_subtitles->exportSubtitles("vtt"); });
    act("file.exportAss", "Export Subtitles as ASS…", QKeySequence(), [this]() { m_subtitles->exportSubtitles("ass"); });
    act("file.close", "Close Project", QKeySequence("Ctrl+Shift+W"), [this]() { if (closeProject()) showWelcome(); });
    act("app.prefs", "Preferences…", QKeySequence::Preferences, [this]() { openPreferences(false); });
    act("app.quit", "Quit", QKeySequence::Quit, [this]() { close(); });
    m_actions["app.prefs"]->setMenuRole(QAction::PreferencesRole);
    m_actions["app.quit"]->setMenuRole(QAction::QuitRole);

    // Edit
    QAction *undo = m_doc.undoStack()->createUndoAction(this, "Undo");
    QAction *redo = m_doc.undoStack()->createRedoAction(this, "Redo");
    Shortcuts::instance().registerAction("edit.undo", "Undo", QKeySequence::Undo);
    Shortcuts::instance().registerAction("edit.redo", "Redo", QKeySequence("Ctrl+Shift+Z"));
    undo->setObjectName("edit.undo");
    redo->setObjectName("edit.redo");
    addAction(undo);
    addAction(redo);
    m_actions.insert("edit.undo", undo);
    m_actions.insert("edit.redo", redo);
    act("edit.cut", "Cut", QKeySequence::Cut, [this]() { copySelection(true); }, true);
    act("edit.copy", "Copy", QKeySequence::Copy, [this]() { copySelection(false); }, true);
    act("edit.paste", "Paste at Playhead", QKeySequence::Paste, [this]() { paste(); }, true);
    act("edit.duplicate", "Duplicate", QKeySequence("Ctrl+D"), [this]() {
        copySelection(false);
        paste();
    }, true);
    act("edit.delete", "Delete", QKeySequence(Qt::Key_Delete), [this]() { deleteSelection(false); }, true);
    act("edit.deleteBack", "Delete (Backspace)", QKeySequence(Qt::Key_Backspace), [this]() { deleteSelection(false); }, true);
    act("edit.ripple", "Ripple Delete", QKeySequence("Shift+Delete"), [this]() { deleteSelection(true); }, true);
    act("edit.split", "Split at Playhead", QKeySequence("S"), [this]() { splitAtPlayhead(false); }, true);
    act("edit.splitAlt", "Split at Playhead (alt.)", QKeySequence("Ctrl+B"), [this]() { splitAtPlayhead(false); }, true);
    act("edit.splitAll", "Split All Tracks at Playhead", QKeySequence("Shift+S"), [this]() { splitAtPlayhead(true); }, true);
    act("edit.selectAll", "Select All Clips", QKeySequence::SelectAll, [this]() {
        QStringList ids;
        for (const auto &t : m_doc.project().tracks)
            for (const auto &c : t.clips)
                ids << c.id;
        m_doc.setSelectedClips(ids);
    }, true);
    act("edit.deselect", "Deselect", QKeySequence(Qt::Key_Escape), [this]() { m_doc.setSelectedClips({}); }, true);
    act("edit.unlink", "Link / Unlink Audio and Video", QKeySequence("Ctrl+L"), [this]() {
        const QStringList sel = m_doc.selectedClips();
        bool anyLinked = false;
        for (const QString &id : sel)
            if (const Clip *c = m_doc.project().findClip(id))
                anyLinked = anyLinked || !c->linkId.isEmpty();
        m_doc.apply(anyLinked ? "Unlink" : "Link", [&](Project &p) {
            return anyLinked ? TimelineOps::unlinkClips(p, sel) : TimelineOps::linkClips(p, sel);
        });
    }, true);

    // Playback
    act("play.toggle", "Play / Pause", QKeySequence(Qt::Key_Space), [this]() { m_engine->togglePlay(); }, true);
    act("play.j", "Shuttle Reverse (J)", QKeySequence("J"), [this]() { m_engine->shuttle(-1); }, true);
    act("play.k", "Stop (K)", QKeySequence("K"), [this]() { m_engine->shuttle(0); }, true);
    act("play.l", "Shuttle Forward (L)", QKeySequence("L"), [this]() { m_engine->shuttle(1); }, true);
    act("play.prevFrame", "Previous Frame", QKeySequence(Qt::Key_Left), [this]() { m_engine->stepFrames(-1); }, true);
    act("play.nextFrame", "Next Frame", QKeySequence(Qt::Key_Right), [this]() { m_engine->stepFrames(1); }, true);
    act("play.back10", "Back 10 Frames", QKeySequence("Shift+Left"), [this]() { m_engine->stepFrames(-10); }, true);
    act("play.fwd10", "Forward 10 Frames", QKeySequence("Shift+Right"), [this]() { m_engine->stepFrames(10); }, true);
    act("play.start", "Go to Start", QKeySequence(Qt::Key_Home), [this]() { m_engine->seek(0); }, true);
    act("play.end", "Go to End", QKeySequence(Qt::Key_End), [this]() { m_engine->seek(m_doc.project().duration()); }, true);
    act("play.prevEdit", "Previous Edit", QKeySequence(Qt::Key_Up), [this]() { jumpEdit(-1); }, true);
    act("play.nextEdit", "Next Edit", QKeySequence(Qt::Key_Down), [this]() { jumpEdit(1); }, true);
    auto *loop = act("play.loop", "Loop In/Out", QKeySequence("Ctrl+Shift+L"), [this]() {
        m_engine->setLoopInOut(m_actions["play.loop"]->isChecked());
    });
    loop->setCheckable(true);

    // Marks
    act("mark.in", "Set In Point", QKeySequence("I"), [this]() {
        const TimeUs out = m_doc.project().outPoint > m_doc.playhead() ? m_doc.project().outPoint : -1;
        m_doc.setInOut(m_doc.playhead(), out);
    }, true);
    act("mark.out", "Set Out Point", QKeySequence("O"), [this]() {
        const TimeUs in = m_doc.project().inPoint >= 0 && m_doc.project().inPoint < m_doc.playhead() ? m_doc.project().inPoint : 0;
        m_doc.setInOut(in, m_doc.playhead());
    }, true);
    act("mark.clear", "Clear In/Out", QKeySequence("Alt+X"), [this]() { m_doc.setInOut(-1, -1); }, true);

    // Timeline
    act("tl.zoomIn", "Zoom In", QKeySequence("="), [this]() { m_timeline->zoomIn(); }, true);
    act("tl.zoomOut", "Zoom Out", QKeySequence("-"), [this]() { m_timeline->zoomOut(); }, true);
    act("tl.fit", "Zoom to Fit", QKeySequence("Shift+Z"), [this]() { m_timeline->zoomToFit(); }, true);
    auto *snap = act("tl.snap", "Snapping", QKeySequence("N"), [this]() {
        const bool on = m_actions["tl.snap"]->isChecked();
        m_timeline->setSnapping(on);
        Settings::instance().snapping = on;
        Settings::instance().save();
    }, true);
    snap->setCheckable(true);
    snap->setChecked(Settings::instance().snapping);
    m_timeline->setSnapping(Settings::instance().snapping);
    act("tl.addVideo", "Add Video Track", QKeySequence(), [this]() {
        m_doc.apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Video).isEmpty(); });
    });
    act("tl.addAudio", "Add Audio Track", QKeySequence(), [this]() {
        m_doc.apply("Add track", [](Project &p) { return !TimelineOps::addTrack(p, TrackKind::Audio).isEmpty(); });
    });
    act("tl.title", "Add Title at Playhead", QKeySequence("T"), [this]() { m_effects->applyEffect("title-center"); }, true);
    act("tl.lowerThird", "Add Lower Third", QKeySequence("Shift+T"), [this]() { m_effects->applyEffect("title-lower"); }, true);

    // Tools (all local)
    auto firstSelected = [this]() { return m_doc.selectedClips().isEmpty() ? QString() : m_doc.selectedClips().first(); };
    act("tools.silences", "Remove Silences…", QKeySequence(), [this]() { m_tools->removeSilences(); });
    act("tools.fillers", "Remove Filler Words (um, uh)…", QKeySequence(), [this]() { m_tools->removeFillerWords(); });
    act("tools.scenes", "Split by Scenes", QKeySequence(), [this]() { m_tools->splitScenes(); });
    act("tools.beats", "Find Beats (add markers)", QKeySequence(), [this]() { m_tools->detectBeats(); });
    act("tools.tts", "Text to Speech…", QKeySequence(), [this]() { m_tools->textToSpeech(); });
    act("tools.reverse", "Reverse Clip", QKeySequence(), [this, firstSelected]() { m_tools->reverseClip(firstSelected()); });
    act("tools.freeze", "Freeze Frame", QKeySequence("Shift+F"), [this]() { m_tools->freezeFrame(); }, true);
    act("tools.stabilize", "Stabilise Clip", QKeySequence(), [this, firstSelected]() { m_tools->stabilizeClip(firstSelected()); });
    act("tools.extractAudio", "Extract Audio", QKeySequence(), [this, firstSelected]() { m_tools->extractAudio(firstSelected()); });
    act("tools.snapshot", "Save Frame as Image…", QKeySequence("Ctrl+Shift+S"), [this]() { m_tools->snapshot(); });
    act("mark.add", "Add / Remove Marker", QKeySequence("M"), [this]() { m_tools->addMarker(); }, true);
    act("mark.prev", "Previous Marker", QKeySequence("Shift+M"), [this]() { m_tools->jumpMarker(-1); }, true);
    act("mark.next", "Next Marker", QKeySequence("Alt+M"), [this]() { m_tools->jumpMarker(1); }, true);
    act("mark.clearAll", "Clear All Markers", QKeySequence(), [this]() { m_tools->clearMarkers(); });
    act("lib.text", "Add Text…", QKeySequence(), [this]() {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory("Text");
    });
    act("lib.effects", "Add Effect…", QKeySequence(), [this]() {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory("Effects");
    });
    act("lib.transitions", "Add Transition…", QKeySequence(), [this]() {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory("Transitions");
    });
    act("lib.filters", "Add Filter…", QKeySequence(), [this]() {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory("Filters");
    });
    act("lib.stickers", "Add Sticker…", QKeySequence(), [this]() {
        m_effectsDock->show();
        m_effectsDock->raise();
        m_effects->showCategory("Stickers");
    });
    act("clip.keyframe", "Add Keyframe", QKeySequence("Ctrl+K"), [this]() {
        const TimeUs ph = m_doc.playhead();
        const QStringList ids = m_doc.selectedClips();
        if (ids.isEmpty())
            return statusBar()->showMessage("Select a clip to keyframe", 3000);
        m_doc.apply("Keyframe", [&](Project &p) {
            bool any = false;
            for (const QString &id : ids)
                if (Clip *c = p.findClip(id); c && c->type != ClipType::Effect) {
                    const TimeUs local = std::clamp<TimeUs>(ph - c->start, 0, c->duration());
                    if (Keyframes::indexAt(c->fx.keyframes, local, 20 * kUsPerMs) >= 0)
                        continue;
                    const AnimatedValues v = c->animatedAt(c->start + local).baseValues();
                    c->fx.keyframes << Keyframe{local, v.x, v.y, v.scale, v.rotation, v.opacity, "smooth"};
                    Keyframes::sort(c->fx.keyframes);
                    any = true;
                }
            return any;
        });
        m_inspectorDock->show();
        m_inspectorDock->raise();
        m_inspector->showTab("Animate");
    }, true);

    // Captions
    act("cap.generate", "Generate Captions…", QKeySequence("Ctrl+Shift+C"), [this]() {
        m_subtitleDock->show();
        m_subtitleDock->raise();
        m_subtitles->showTab(0);
    });
    act("cap.style", "Caption Styles…", QKeySequence(), [this]() {
        m_subtitleDock->show();
        m_subtitleDock->raise();
        m_subtitles->showTab(2);
    });
    act("cap.edit", "Edit Transcript…", QKeySequence(), [this]() {
        m_subtitleDock->show();
        m_subtitleDock->raise();
        m_subtitles->showTab(1);
    });

    // View
    act("view.quick", "Quick Editor", QKeySequence("Ctrl+1"), [this]() { setQuickMode(true); });
    act("view.advanced", "Advanced Editor", QKeySequence("Ctrl+2"), [this]() { setQuickMode(false); });
    auto *safe = act("view.safe", "Show Caption-Safe Area", QKeySequence("'"), [this]() {
        m_engine->setShowSafeArea(m_actions["view.safe"]->isChecked());
    }, true);
    safe->setCheckable(true);
    auto *hc = act("view.contrast", "High-Contrast Theme", QKeySequence(), [this]() {
        Settings::instance().highContrast = m_actions["view.contrast"]->isChecked();
        Settings::instance().save();
        applyTheme();
    });
    hc->setCheckable(true);
    hc->setChecked(Settings::instance().highContrast);

    // Help
    act("help.shortcuts", "Keyboard Shortcuts", QKeySequence("Ctrl+/"), [this]() { showShortcutsHelp(); });
    act("help.privacy", "Privacy Statement", QKeySequence(), [this]() { showPrivacy(); });
    act("help.about", "About Shyftr Studio", QKeySequence(), [this]() { showAbout(); });
    m_actions["help.about"]->setMenuRole(QAction::AboutRole);
}

void MainWindow::applyShortcuts()
{
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
        const QKeySequence k = Shortcuts::instance().key(it.key());
        it.value()->setShortcut(k);
        if (!k.isEmpty())
            it.value()->setToolTip(it.value()->text() + " (" + k.toString(QKeySequence::NativeText) + ")");
    }
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu("&File");
    for (const char *id : {"file.new", "file.open"})
        file->addAction(m_actions[id]);
    auto *recent = file->addMenu("Open Recent");
    connect(recent, &QMenu::aboutToShow, this, [this, recent]() {
        recent->clear();
        for (const QString &d : RecentProjects::list())
            recent->addAction(QDir::toNativeSeparators(d), [this, d]() { openProject(d); });
        if (recent->isEmpty())
            recent->addAction("No recent projects")->setEnabled(false);
    });
    file->addSeparator();
    file->addAction(m_actions["file.save"]);
    file->addAction(m_actions["file.package"]);
    file->addAction(m_actions["file.collect"]);
    file->addAction(m_actions["file.relink"]);
    file->addSeparator();
    file->addAction(m_actions["file.import"]);
    file->addAction(m_actions["file.importSubs"]);
    file->addAction(m_actions["file.record"]);
    file->addSeparator();
    file->addAction(m_actions["file.export"]);
    auto *subs = file->addMenu("Export Subtitles");
    subs->addAction(m_actions["file.exportSrt"]);
    subs->addAction(m_actions["file.exportVtt"]);
    subs->addAction(m_actions["file.exportAss"]);
    file->addSeparator();
    file->addAction(m_actions["file.close"]);
    file->addAction(m_actions["app.prefs"]);
    file->addAction(m_actions["app.quit"]);

    auto *edit = menuBar()->addMenu("&Edit");
    for (const char *id : {"edit.undo", "edit.redo"})
        edit->addAction(m_actions[id]);
    edit->addSeparator();
    for (const char *id : {"edit.cut", "edit.copy", "edit.paste", "edit.duplicate"})
        edit->addAction(m_actions[id]);
    edit->addSeparator();
    for (const char *id : {"edit.split", "edit.splitAll", "edit.delete", "edit.ripple", "edit.unlink"})
        edit->addAction(m_actions[id]);
    edit->addSeparator();
    edit->addAction(m_actions["edit.selectAll"]);
    edit->addAction(m_actions["edit.deselect"]);

    auto *tl = menuBar()->addMenu("&Timeline");
    for (const char *id : {"tl.addVideo", "tl.addAudio", "tl.title", "tl.lowerThird"})
        tl->addAction(m_actions[id]);
    tl->addSeparator();
    for (const char *id : {"mark.in", "mark.out", "mark.clear"})
        tl->addAction(m_actions[id]);
    tl->addSeparator();
    for (const char *id : {"tl.snap", "tl.zoomIn", "tl.zoomOut", "tl.fit"})
        tl->addAction(m_actions[id]);

    tl->addSeparator();
    for (const char *id : {"mark.add", "mark.prev", "mark.next", "mark.clearAll"})
        tl->addAction(m_actions[id]);

    auto *clipMenu = menuBar()->addMenu("C&lip");
    for (const char *id : {"clip.keyframe", "tools.freeze", "tools.reverse", "tools.stabilize", "tools.extractAudio"})
        clipMenu->addAction(m_actions[id]);
    clipMenu->addSeparator();
    for (const char *id : {"lib.text", "lib.stickers", "lib.effects", "lib.transitions", "lib.filters"})
        clipMenu->addAction(m_actions[id]);

    auto *tools = menuBar()->addMenu("T&ools");
    for (const char *id : {"tools.silences", "tools.fillers", "tools.scenes", "tools.beats"})
        tools->addAction(m_actions[id]);
    tools->addSeparator();
    tools->addAction(m_actions["tools.tts"]);
    tools->addAction(m_actions["tools.snapshot"]);

    auto *play = menuBar()->addMenu("&Playback");
    for (const char *id : {"play.toggle", "play.j", "play.k", "play.l", "play.prevFrame", "play.nextFrame", "play.back10",
                           "play.fwd10", "play.start", "play.end", "play.prevEdit", "play.nextEdit", "play.loop"})
        play->addAction(m_actions[id]);

    auto *caps = menuBar()->addMenu("&Captions");
    for (const char *id : {"cap.generate", "cap.edit", "cap.style", "file.importSubs"})
        caps->addAction(m_actions[id]);
    caps->addSeparator();
    caps->addMenu(subs);

    auto *view = menuBar()->addMenu("&View");
    view->addAction(m_actions["view.quick"]);
    view->addAction(m_actions["view.advanced"]);
    view->addSeparator();
    view->addAction(m_actions["view.safe"]);
    view->addAction(m_actions["view.contrast"]);
    view->addSeparator();
    for (QDockWidget *d : {m_mediaDock, m_effectsDock, m_inspectorDock, m_subtitleDock, m_timelineDock, m_quickDock})
        view->addAction(d->toggleViewAction());

    auto *help = menuBar()->addMenu("&Help");
    for (const char *id : {"help.shortcuts", "help.privacy", "help.about"})
        help->addAction(m_actions[id]);

    // toolbar
    auto *tb = addToolBar("Main");
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *quick = tb->addAction("Quick");
    auto *adv = tb->addAction("Advanced");
    quick->setCheckable(true);
    adv->setCheckable(true);
    auto *grp = new QActionGroup(this);
    grp->addAction(quick);
    grp->addAction(adv);
    connect(quick, &QAction::triggered, this, [this]() { setQuickMode(true); });
    connect(adv, &QAction::triggered, this, [this]() { setQuickMode(false); });
    connect(m_actions["view.quick"], &QAction::triggered, quick, [quick]() { quick->setChecked(true); });
    connect(m_actions["view.advanced"], &QAction::triggered, adv, [adv]() { adv->setChecked(true); });
    quick->setObjectName("tb.quick");
    adv->setObjectName("tb.advanced");
    tb->addSeparator();
    for (const char *id : {"edit.undo", "edit.redo"})
        tb->addAction(m_actions[id]);
    tb->addSeparator();
    tb->addAction(m_actions["file.import"]);
    tb->addAction(m_actions["edit.split"]);
    tb->addAction(m_actions["tl.snap"]);
    tb->addAction(m_actions["cap.generate"]);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction(m_actions["file.export"]);
}

void MainWindow::applyTheme()
{
    Theme::apply(*qApp, Settings::instance().highContrast, Settings::instance().uiScale);
    m_timeline->viewport()->update();
}

// ================================================================== project lifecycle

void MainWindow::showWelcome()
{
    m_welcome->refresh();
    m_stack->setCurrentWidget(m_welcome);
    for (QDockWidget *d : {m_mediaDock, m_effectsDock, m_inspectorDock, m_subtitleDock, m_timelineDock, m_quickDock})
        d->hide();
    for (QToolBar *tb : findChildren<QToolBar *>())
        tb->hide();
    setWindowTitle("Shyftr Studio");
    m_saveState->clear();
}

void MainWindow::showEditor()
{
    m_stack->setCurrentWidget(m_viewer);
    for (QToolBar *tb : findChildren<QToolBar *>())
        tb->show();
    const bool quick = m_doc.project().uiMode.isEmpty() ? Settings::instance().startInQuickMode : m_doc.project().uiMode == "quick";
    m_quickMode = !quick; // force apply
    setQuickMode(quick);
}

static QString layoutKey(bool quick)
{
    // v2: responsive layouts (older saved layouts from fixed-size builds are ignored)
    return quick ? "layout/v3/quick" : "layout/v3/advanced";
}

void MainWindow::applyDockProportions(bool quick)
{
    const int W = width(), H = height();
    const int timelineH = std::clamp(int(H * 0.30), 160, 440);
    resizeDocks({m_timelineDock}, {timelineH}, Qt::Vertical);
    if (quick) {
        const int right = std::clamp(int(W * 0.27), 300, 460);
        if (m_effectsDock->isVisible()) {
            const int left = std::clamp(int(W * 0.17), 236, 300); // two library columns
            resizeDocks({m_effectsDock, m_quickDock}, {left, right}, Qt::Horizontal);
        } else {
            resizeDocks({m_quickDock}, {right}, Qt::Horizontal);
        }
    } else {
        const int left = std::clamp(int(W * 0.17), 236, 340);
        const int right = std::clamp(int(W * 0.25), 280, 480);
        resizeDocks({m_mediaDock, m_inspectorDock}, {left, right}, Qt::Horizontal);
    }
}

void MainWindow::setQuickMode(bool quick)
{
    if (!m_doc.isOpen())
        return;
    QSettings qs;
    if (quick == m_quickMode && m_timelineDock->isVisible())
        return;
    if (m_stack->currentWidget() == m_viewer && m_timelineDock->isVisible())
        qs.setValue(layoutKey(m_quickMode), saveState());
    m_quickMode = quick;
    m_timeline->setCompact(quick);
    const QByteArray st = qs.value(layoutKey(quick)).toByteArray();
    const bool restored = !st.isEmpty() && restoreState(st);
    if (!restored) {
        // Quick: creator steps + caption editor share the right column.
        // Advanced: media/effects left, inspector/subtitles right.
        if (quick) {
            addDockWidget(Qt::RightDockWidgetArea, m_quickDock);
            addDockWidget(Qt::RightDockWidgetArea, m_subtitleDock);
            tabifyDockWidget(m_quickDock, m_subtitleDock);
        } else {
            addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
            addDockWidget(Qt::RightDockWidgetArea, m_subtitleDock);
            tabifyDockWidget(m_inspectorDock, m_subtitleDock);
        }
        m_quickDock->setVisible(quick);
        m_subtitleDock->setVisible(true);
        for (QDockWidget *d : {m_mediaDock, m_inspectorDock})
            d->setVisible(!quick);
        // the creative library stays on the left in Quick mode when there is room for it
        m_effectsDock->setVisible(!quick || width() >= 1180);
        if (quick)
            m_effectsDock->raise();
        if (!quick) {
            m_mediaDock->raise();
            m_inspectorDock->raise();
        }
    }
    m_timelineDock->show();
    if (quick) {
        m_quickDock->show();
        if (!restored)
            m_quickDock->raise();
    }
    if (auto *a = findChild<QAction *>(quick ? "tb.quick" : "tb.advanced"))
        a->setChecked(true);
    const QString mode = quick ? "quick" : "advanced";
    if (m_doc.project().uiMode != mode)
        m_doc.updateMeta([mode](Project &p) { p.uiMode = mode; });
    QTimer::singleShot(0, this, [this, quick, restored]() {
        // Re-fit when there is no saved layout, or the saved one leaves the video too small
        // (e.g. it was saved on a bigger display).
        const bool cramped = m_viewer->width() < width() * 0.35 || m_viewer->height() < height() * 0.30;
        if (!restored || cramped)
            applyDockProportions(quick);
    });
    updateTitle();
}

void MainWindow::fitToScreen()
{
    QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!scr)
        return;
    const QRect avail = scr->availableGeometry();
    QSettings qs;
    const QByteArray geo = qs.value("window/geometry").toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);
    const bool fits = !geo.isEmpty() && avail.contains(frameGeometry().adjusted(8, 8, -8, -8));
    if (!fits) {
        const QSize sz(std::min(int(avail.width() * 0.92), 1760), std::min(int(avail.height() * 0.92), 1100));
        resize(sz);
        move(avail.center() - QPoint(sz.width() / 2, sz.height() / 2));
    }
    if (avail.width() < 1400 && geo.isEmpty())
        setWindowState(windowState() | Qt::WindowMaximized);
}

void MainWindow::createProject(const QString &nameIn, const QString &parentDir, const CanvasSettings &canvas)
{
    if (!closeProject())
        return;
    const QString name = nameIn.isEmpty() ? "Untitled project" : nameIn;
    QString dir = parentDir + "/" + ProjectStore::sanitizeName(name);
    int n = 2;
    while (QFileInfo::exists(dir))
        dir = parentDir + "/" + ProjectStore::sanitizeName(name) + QString(" %1").arg(n++);
    Project p = makeEmptyProject(name, canvas);
    p.uiMode = Settings::instance().startInQuickMode ? "quick" : "advanced";
    QString err;
    if (!QDir().mkpath(dir) || !ProjectStore::createProject(dir, p, &err)) {
        QMessageBox::critical(this, "New project", "The project could not be created.\n" + err);
        return;
    }
    openProject(dir);
}

void MainWindow::openProjectDialog()
{
    const QString f = QFileDialog::getOpenFileName(this, "Open project", AppPaths::projectsDir(), "Shyftr Studio project (project.json)");
    if (!f.isEmpty())
        openProject(QFileInfo(f).absolutePath());
}

bool MainWindow::openProject(const QString &dirIn, bool recover)
{
    const QString dir = QFileInfo(dirIn).isFile() ? QFileInfo(dirIn).absolutePath() : QDir(dirIn).absolutePath();
    if (m_doc.isOpen() && m_doc.projectDir() == dir) {
        showEditor();
        return true;
    }
    if (!closeProject())
        return false;
    if (!QFileInfo::exists(dir + "/project.json")) {
        QMessageBox::warning(this, "Open project", "No project.json was found in " + QDir::toNativeSeparators(dir));
        RecentProjects::remove(dir);
        m_welcome->refresh();
        return false;
    }
    auto *lock = new QLockFile(dir + "/.lock");
    lock->setStaleLockTime(0); // stale detection by process id only
    if (!lock->tryLock(200)) {
        delete lock;
        QMessageBox::warning(this, "Open project", "This project is already open in another Shyftr Studio window.");
        return false;
    }

    const RecoveryInfo ri = ProjectStore::recoveryInfo(dir);
    if (!recover && ri.uncleanShutdown && ri.autosaveIsNewer()) {
        const auto r = QMessageBox::question(
            this, "Recover unsaved changes",
            QString("Shyftr Studio closed unexpectedly while this project was open.\n\nAn autosave from %1 is newer than "
                    "the last save (%2). Recover it?\n\nThe saved project.json is not changed until you save.")
                .arg(ri.autosaveTime.toString("d MMM HH:mm:ss"), ri.projectTime.toString("d MMM HH:mm:ss")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        recover = r == QMessageBox::Yes;
    }

    Project p;
    LoadReport rep;
    QString err;
    bool ok;
    if (recover && !ri.autosavePath.isEmpty()) {
        ok = ProjectStore::loadFile(ri.autosavePath, p, &rep, &err);
        if (ok)
            ProjectStore::refreshMediaPaths(p, dir, &rep);
    } else {
        ok = ProjectStore::load(dir, p, &rep, &err);
    }
    if (!ok) {
        lock->unlock();
        delete lock;
        QMessageBox::critical(this, "Open project", "The project could not be opened.\n\n" + err);
        return false;
    }
    if (rep.migratedFromVersion > 0) {
        QDir(dir).mkpath("backups");
        QFile::copy(dir + "/project.json",
                    dir + QString("/backups/project-pre-migration-v%1.json").arg(rep.migratedFromVersion));
    }
    m_lock = lock;
    m_cache->setProjectDir(dir);
    TemplateLibrary::instance().reload(dir + "/templates");
    m_doc.undoStack()->setUndoLimit(Settings::instance().undoLimit);
    m_doc.reset(p, dir);
    if (recover || rep.migratedFromVersion > 0 || !rep.resolvedViaRelative.isEmpty())
        m_doc.markDirty();
    ProjectStore::markSessionOpen(dir);
    RecentProjects::add(dir);
    m_lastAutosaveCounter = m_doc.changeCounter();
    m_autosave.start(Settings::instance().autosaveIntervalSec * 1000);
    showEditor();
    m_timeline->zoomToFit();
    m_engine->seek(p.playhead);
    updateTitle();

    if (recover)
        statusBar()->showMessage("Recovered unsaved changes from autosave. Save to keep them.", 8000);
    if (rep.migratedFromVersion > 0)
        QMessageBox::information(this, "Project upgraded",
                                 QString("This project was created with an older format (schema %1) and has been upgraded. "
                                         "The original file was kept in backups/.")
                                     .arg(rep.migratedFromVersion));
    if (!rep.warning.isEmpty())
        QMessageBox::warning(this, "Project", rep.warning);
    if (!rep.missingMediaIds.isEmpty()) {
        const auto r = QMessageBox::question(this, "Media offline",
                                             QString("%1 media file(s) could not be found. Relink them now?").arg(rep.missingMediaIds.size()));
        if (r == QMessageBox::Yes)
            relinkMedia();
    }
    return true;
}

bool MainWindow::saveProject()
{
    if (!m_doc.isOpen())
        return false;
    QString err;
    if (!ProjectStore::save(m_doc.project(), m_doc.projectDir(), &err, Settings::instance().backupKeep)) {
        QMessageBox::critical(this, "Save failed",
                              "The project could not be saved. Your previous save and autosaves are intact.\n\n" + err);
        return false;
    }
    m_doc.markSaved();
    m_lastAutosaveCounter = m_doc.changeCounter();
    m_saveState->setText("Saved " + QTime::currentTime().toString("HH:mm"));
    statusBar()->showMessage("Project saved", 2500);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_doc.isOpen() || !m_doc.isDirty())
        return true;
    const auto r = QMessageBox::question(this, "Unsaved changes", "Save changes to \"" + m_doc.project().name + "\"?",
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return saveProject();
    return true;
}

bool MainWindow::closeProject()
{
    if (!m_doc.isOpen())
        return true;
    if (m_exportDialog && m_exportDialog->isVisible()) {
        m_exportDialog->reject();
        if (m_exportDialog && m_exportDialog->isVisible())
            return false;
    }
    if (m_subtitles->isGenerating() &&
        QMessageBox::question(this, "Close project", "Captions are still being generated. Stop and close?") != QMessageBox::Yes)
        return false;
    if (!maybeSave())
        return false;
    m_engine->pause();
    QSettings qs;
    qs.setValue(layoutKey(m_quickMode), saveState());
    const QString dir = m_doc.projectDir();
    m_autosave.stop();
    ProjectStore::markSessionClosed(dir);
    if (!Settings::instance().keepAutosavesOnClose && !m_doc.isDirty())
        for (const QString &a : ProjectStore::autosaves(dir))
            QFile::remove(a);
    m_doc.clear();
    if (m_lock) {
        m_lock->unlock();
        delete m_lock;
        m_lock = nullptr;
    }
    m_cache->clearMemory();
    return true;
}

void MainWindow::autosaveTick()
{
    if (!m_doc.isOpen() || !m_doc.isDirty() || m_doc.changeCounter() == m_lastAutosaveCounter)
        return;
    QString err;
    const QString path = ProjectStore::writeAutosave(m_doc.project(), m_doc.projectDir(), Settings::instance().autosaveKeep, &err);
    if (!path.isEmpty()) {
        m_lastAutosaveCounter = m_doc.changeCounter();
        m_saveState->setText("Autosaved " + QTime::currentTime().toString("HH:mm"));
    } else {
        statusBar()->showMessage("Autosave failed: " + err, 6000);
    }
    m_autosave.setInterval(Settings::instance().autosaveIntervalSec * 1000);
}

void MainWindow::updateTitle()
{
    if (!m_doc.isOpen())
        return;
    setWindowTitle(QString("%1[*] — Shyftr Studio · %2").arg(m_doc.project().name, m_quickMode ? "Quick" : "Advanced"));
    setWindowModified(m_doc.isDirty());
    if (m_doc.isDirty() && !m_saveState->text().startsWith("Autosaved"))
        m_saveState->setText("Unsaved changes");
}

void MainWindow::packageProject()
{
    if (!m_doc.isOpen())
        return;
    const QString dest = QFileDialog::getExistingDirectory(this, "Save portable package to…");
    if (dest.isEmpty())
        return;
    const qint64 bytes = ProjectStore::estimateCollectBytes(m_doc.project(), "/nonexistent") ;
    if (QMessageBox::question(this, "Portable package",
                              QString("Copy the project and all its media (about %1 MB) into a self-contained folder?")
                                  .arg(bytes >> 20)) != QMessageBox::Yes)
        return;
    QProgressDialog prog("Packaging project…", "Cancel", 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    QString err;
    const bool ok = ProjectStore::packageProject(m_doc.project(), m_doc.projectDir(), dest, &err, [&](int d, int t) {
        prog.setValue(t ? d * 100 / t : 0);
        QApplication::processEvents();
        return !prog.wasCanceled();
    });
    prog.close();
    if (ok)
        QMessageBox::information(this, "Portable package", "Package written to " + QDir::toNativeSeparators(dest));
    else
        QMessageBox::warning(this, "Portable package", err);
}

void MainWindow::collectMedia()
{
    if (!m_doc.isOpen())
        return;
    const qint64 bytes = ProjectStore::estimateCollectBytes(m_doc.project(), m_doc.projectDir());
    if (bytes == 0) {
        QMessageBox::information(this, "Collect media", "All media is already inside the project folder.");
        return;
    }
    if (QMessageBox::question(this, "Collect media",
                              QString("Copy %1 MB of media into this project's media folder so the project is portable?")
                                  .arg(bytes >> 20)) != QMessageBox::Yes)
        return;
    Project p = m_doc.project();
    QProgressDialog prog("Collecting media…", "Cancel", 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    QString err;
    const bool ok = ProjectStore::collectMedia(p, m_doc.projectDir(), &err, [&](int d, int t) {
        prog.setValue(t ? d * 100 / t : 0);
        QApplication::processEvents();
        return !prog.wasCanceled();
    });
    prog.close();
    if (!ok) {
        QMessageBox::warning(this, "Collect media", err);
        return;
    }
    m_doc.replaceProjectSilently(p);
    saveProject();
}

void MainWindow::relinkMedia()
{
    if (!m_doc.isOpen())
        return;
    Project p = m_doc.project();
    ProjectStore::refreshMediaPaths(p, m_doc.projectDir(), nullptr);
    bool anyMissing = false;
    for (const auto &m : p.media)
        anyMissing = anyMissing || m.missing;
    if (!anyMissing) {
        QMessageBox::information(this, "Relink media", "All media files are online.");
        m_doc.replaceProjectSilently(p);
        return;
    }
    RelinkDialog dlg(p, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int n = ProjectStore::applyRelink(p, dlg.result(), m_doc.projectDir());
    m_doc.replaceProjectSilently(p);
    m_cache->clearMemory();
    statusBar()->showMessage(QString("Relinked %1 file(s)").arg(n), 5000);
}

// ================================================================== editing

void MainWindow::importDialog()
{
    if (!m_doc.isOpen())
        return;
    const QStringList files = QFileDialog::getOpenFileNames(this, "Import media", QString(), MediaProbe::importFilters().join(";;"));
    if (!files.isEmpty())
        importFiles(files);
}

void MainWindow::importFiles(const QStringList &pathsIn, TimeUs at, const QString &trackId, bool asMusic, bool insert)
{
    if (!m_doc.isOpen())
        return;
    QStringList paths;
    for (const QString &p : pathsIn) {
        if (QFileInfo(p).isDir())
            continue;
        if (p.endsWith(".srt", Qt::CaseInsensitive) || p.endsWith(".vtt", Qt::CaseInsensitive)) {
            m_subtitles->importSubtitles(p);
            continue;
        }
        if (p.endsWith("project.json")) {
            openProject(QFileInfo(p).absolutePath());
            return;
        }
        paths << p;
    }
    if (paths.isEmpty())
        return;
    statusBar()->showMessage(QString("Importing %1 file(s)…").arg(paths.size()));
    struct Result {
        QVector<MediaAsset> assets;
        QStringList errors;
    };
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, at, trackId, asMusic, insert]() {
        const Result r = watcher->result();
        watcher->deleteLater();
        if (!m_doc.isOpen())
            return;
        QStringList added;
        const bool placeAuto = at < 0 && m_quickMode && m_doc.project().duration() == 0;
        m_doc.apply(QString("Import %1 file(s)").arg(r.assets.size()), [&](Project &p) {
            for (MediaAsset m : r.assets) {
                bool dup = false;
                for (const auto &e : p.media)
                    if (QFileInfo(e.path) == QFileInfo(m.path)) {
                        added << e.id;
                        dup = true;
                    }
                if (dup)
                    continue;
                m.id = newId();
                m.relativePath = ProjectStore::relativeTo(m_doc.projectDir(), m.path);
                p.media << m;
                added << m.id;
            }
            if (at >= 0 && !asMusic) {
                // dropped on the timeline: same placement rules as dragging from the media bin
                const QStringList placed =
                    TimelineOps::placeMedia(p, added, trackId, at, insert ? TimelineOps::PlaceMode::Insert : TimelineOps::PlaceMode::Auto);
                Q_UNUSED(placed);
            } else if (at >= 0 || placeAuto || asMusic) {
                TimeUs t = at >= 0 ? at : (asMusic ? 0 : p.duration());
                const TimeUs videoEnd = p.duration();
                for (const QString &id : added) {
                    QString tid = trackId;
                    if (asMusic) {
                        tid = TimelineOps::addTrack(p, TrackKind::Audio, "Music");
                    }
                    const QString cid = TimelineOps::addMediaClip(p, id, tid, t);
                    if (cid.isEmpty())
                        continue;
                    Clip *c = p.findClip(cid);
                    if (asMusic) {
                        c->audio.gainDb = -14;
                        if (videoEnd > 0 && c->end() > videoEnd)
                            c->out = c->in + TimeUs((videoEnd - c->start) * c->speed);
                        c->audio.fadeIn = std::min<TimeUs>(kUsPerSec, c->duration() / 3);
                        c->audio.fadeOut = std::min<TimeUs>(2 * kUsPerSec, c->duration() / 3);
                    } else {
                        t = c->end();
                    }
                }
            }
            return !added.isEmpty();
        });
        if (!r.errors.isEmpty())
            QMessageBox::warning(this, "Import", "Some files could not be imported:\n\n" + r.errors.join("\n"));
        int vfr = 0;
        for (const auto &m : r.assets)
            vfr += m.variableFrameRate ? 1 : 0;
        QString msg = QString("Imported %1 file(s)").arg(r.assets.size());
        if (vfr)
            msg += QString(" · %1 variable-frame-rate file(s) will be conformed to the project frame rate").arg(vfr);
        statusBar()->showMessage(msg, 6000);
        if (placeAuto)
            m_timeline->zoomToFit();
    });
    watcher->setFuture(QtConcurrent::run([paths]() {
        Result r;
        for (const QString &p : paths) {
            MediaAsset m;
            QString err;
            if (MediaProbe::probe(p, m, &err))
                r.assets << m;
            else
                r.errors << QFileInfo(p).fileName() + ": " + err;
        }
        return r;
    }));
}

void MainWindow::addMusic()
{
    if (!m_doc.isOpen())
        return;
    const QString f = QFileDialog::getOpenFileName(this, "Add background music", QString(),
                                                   "Audio (*.mp3 *.m4a *.wav *.aac *.flac *.ogg *.opus *.aif *.aiff)");
    if (!f.isEmpty())
        importFiles({f}, -1, {}, true);
}

void MainWindow::recordVoiceOver()
{
    if (!m_doc.isOpen())
        return;
    RecordDialog dlg(m_doc.projectDir(), this);
    if (dlg.exec() == QDialog::Accepted && !dlg.recordedFile().isEmpty() && QFileInfo::exists(dlg.recordedFile()))
        importFiles({dlg.recordedFile()}, m_doc.playhead());
}

void MainWindow::openExport(const QString &presetId)
{
    if (!m_doc.isOpen())
        return;
    if (m_doc.project().duration() <= 0) {
        QMessageBox::information(this, "Export", "Add some media to the timeline first.");
        return;
    }
    if (!m_exportDialog) {
        m_exportDialog = new ExportDialog(&m_doc, this);
        m_exportDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    if (!presetId.isEmpty())
        m_exportDialog->setPreset(presetId);
    m_exportDialog->show();
    m_exportDialog->raise();
    m_exportDialog->activateWindow();
}

void MainWindow::openPreferences(bool models)
{
    PreferencesDialog dlg(this);
    if (models)
        dlg.showModelsTab();
    connect(&dlg, &PreferencesDialog::settingsChanged, this, [this]() {
        applyShortcuts();
        applyTheme();
        m_doc.undoStack()->setUndoLimit(std::max(m_doc.undoStack()->count(), Settings::instance().undoLimit));
        m_engine->setMaxPreviewWidth(Settings::instance().previewMaxWidth);
        if (m_doc.isOpen())
            m_autosave.setInterval(Settings::instance().autosaveIntervalSec * 1000);
    });
    connect(&dlg, &PreferencesDialog::modelsChanged, m_subtitles, &SubtitlePanel::reloadModels);
    dlg.exec();
    m_subtitles->reloadModels();
}

void MainWindow::splitAtPlayhead(bool allTracks)
{
    if (!m_doc.isOpen())
        return;
    const TimeUs t = m_doc.playhead();
    const QStringList sel = m_doc.selectedClips();
    const bool ok = m_doc.apply(allTracks ? "Split all tracks" : "Split", [&](Project &p) {
        if (allTracks || sel.isEmpty()) {
            if (!allTracks) {
                // split the topmost clip under the playhead (and its linked partner)
                for (int i = p.tracks.size() - 1; i >= 0; --i)
                    if (p.tracks[i].kind != TrackKind::Caption && !p.tracks[i].locked)
                        for (const auto &c : p.tracks[i].clips)
                            if (c.containsTimeline(t) && c.start != t)
                                return TimelineOps::splitClip(p, c.id, t);
                return false;
            }
            return TimelineOps::splitAll(p, t) > 0;
        }
        bool any = false;
        for (const QString &id : sel)
            if (const Clip *c = p.findClip(id); c && c->containsTimeline(t))
                any = TimelineOps::splitClip(p, id, t) || any;
        return any;
    });
    if (!ok)
        statusBar()->showMessage("Move the playhead over a clip to split it", 3000);
}

void MainWindow::deleteSelection(bool ripple)
{
    const QStringList sel = m_doc.selectedClips();
    if (sel.isEmpty()) {
        // delete selected caption if the timeline caption lane has focus
        if (!m_doc.selectedCaptionTrack().isEmpty() && m_doc.selectedCue() >= 0 && m_timeline->hasFocus()) {
            const QString tid = m_doc.selectedCaptionTrack();
            const int cue = m_doc.selectedCue();
            m_doc.apply("Delete caption", [&](Project &p) {
                Track *t = p.findTrack(tid);
                Transcript *tr = t ? p.findTranscript(t->transcriptId) : nullptr;
                return tr && TranscriptEdit::deleteCue(*tr, cue);
            });
        }
        return;
    }
    m_doc.apply(ripple ? "Ripple delete" : "Delete", [&](Project &p) { return TimelineOps::deleteClips(p, sel, ripple); });
}

void MainWindow::copySelection(bool cut)
{
    const Project &p = m_doc.project();
    m_clipboard.clear();
    TimeUs minStart = std::numeric_limits<TimeUs>::max();
    QStringList ids;
    for (const QString &id : m_doc.selectedClips())
        for (Clip *c : const_cast<Project &>(p).linkedClips(id))
            if (!ids.contains(c->id))
                ids << c->id;
    for (const QString &id : ids)
        if (const Clip *c = p.findClip(id))
            minStart = std::min(minStart, c->start);
    for (const QString &id : ids) {
        const Track *t = nullptr;
        const Clip *c = p.findClip(id, &t);
        if (!c)
            continue;
        int ordinal = 0;
        for (int i : p.trackIndicesOfKind(t->kind)) {
            if (p.tracks[i].id == t->id)
                break;
            ++ordinal;
        }
        m_clipboard << ClipboardItem{*c, t->kind, ordinal, c->start - minStart};
    }
    if (cut && !ids.isEmpty())
        m_doc.apply("Cut", [&](Project &pr) { return TimelineOps::deleteClips(pr, ids, false); });
    statusBar()->showMessage(QString("%1 clip(s) %2").arg(m_clipboard.size()).arg(cut ? "cut" : "copied"), 2000);
}

void MainWindow::paste()
{
    if (m_clipboard.isEmpty() || !m_doc.isOpen())
        return;
    const TimeUs at = m_doc.playhead();
    QStringList pasted;
    m_doc.apply("Paste", [&](Project &p) {
        QHash<QString, QString> linkMap;
        for (const ClipboardItem &item : m_clipboard) {
            if (item.clip.type == ClipType::Media && !p.findMedia(item.clip.mediaId))
                continue;
            QVector<int> idx = p.trackIndicesOfKind(item.kind);
            while (idx.size() <= item.trackOrdinal) {
                TimelineOps::addTrack(p, item.kind);
                idx = p.trackIndicesOfKind(item.kind);
            }
            const QString tid = p.tracks[idx[item.trackOrdinal]].id;
            const QString id = TimelineOps::insertClipCopy(p, tid, item.clip, at + item.offset);
            if (id.isEmpty())
                continue;
            pasted << id;
            if (!item.clip.linkId.isEmpty()) {
                if (!linkMap.contains(item.clip.linkId))
                    linkMap[item.clip.linkId] = newId();
                p.findClip(id)->linkId = linkMap[item.clip.linkId];
            }
        }
        return !pasted.isEmpty();
    });
    m_doc.setSelectedClips(pasted);
}

void MainWindow::jumpEdit(int dir)
{
    const auto pts = TimelineOps::snapPoints(m_doc.project());
    const TimeUs t = m_doc.playhead();
    TimeUs best = dir < 0 ? 0 : m_doc.project().duration();
    for (TimeUs p : pts) {
        if (dir < 0 && p < t - 1000 && p > best)
            best = p;
        if (dir > 0 && p > t + 1000 && p < best)
            best = p;
    }
    m_engine->seek(best);
}

// ================================================================== window events / help

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!closeProject()) {
        e->ignore();
        return;
    }
    QSettings qs;
    qs.setValue("window/geometry", saveGeometry());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (paths.size() == 1 && (paths.first().endsWith("project.json") || QFileInfo::exists(paths.first() + "/project.json"))) {
        openProject(paths.first());
        return;
    }
    if (m_doc.isOpen())
        importFiles(paths);
}

void MainWindow::editTitleInViewer(const QString &clipId, const QRect &anchor)
{
    const Clip *c = m_doc.project().findClip(clipId);
    if (!c || c->type != ClipType::Title)
        return;
    m_engine->pause();
    // single-line editor: line breaks are shown as " | "
    QString text = c->title.text;
    text.replace('\n', " | ");
    m_viewer->startInlineEdit(text, [this, clipId](const QString &newText) {
        QString t = newText;
        t.replace(" | ", "\n");
        if (m_doc.apply("Edit title", [&](Project &p) {
                Clip *cl = p.findClip(clipId);
                if (!cl || cl->title.text == t)
                    return false;
                cl->title.text = t;
                return true;
            }))
            statusBar()->showMessage("Title updated", 2500);
    }, anchor);
}

void MainWindow::editCaptionInViewer(const QRect &anchor)
{
    if (!m_doc.isOpen())
        return;
    m_engine->pause();
    const TimeUs t = m_doc.playhead();
    QString text;
    if (!m_subtitles->captionAt(t, nullptr, nullptr, &text)) {
        statusBar()->showMessage("Move the playhead onto a caption, then double-click the video to edit it", 4000);
        return;
    }
    m_viewer->startInlineEdit(text, [this, t](const QString &newText) {
        if (m_subtitles->editCaptionAt(t, newText))
            statusBar()->showMessage("Caption updated", 2500);
    }, anchor);
}

void MainWindow::showSubtitleTab(int index)
{
    m_subtitleDock->show();
    m_subtitleDock->raise();
    m_subtitles->showTab(index);
}

void MainWindow::showShortcutsHelp()
{
    QString html = "<h3>Keyboard shortcuts</h3><table cellspacing=6>";
    for (const auto &e : Shortcuts::instance().entries())
        if (!e.key.isEmpty())
            html += QString("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(e.key.toString(QKeySequence::NativeText).toHtmlEscaped(), e.label.toHtmlEscaped());
    html += "</table><p>Mouse: drag clips to move (Alt = ignore links, Shift = no snapping, Ctrl/⌘ = insert and push "
            "clips aside), drag edges to trim, drag the dots at the top corners to fade, Ctrl/⌘+wheel to zoom. "
            "Drag media, files and Library items onto the timeline; drop on a busy spot for a new track, or on the "
            "dashed lanes. Drag track headers to reorder tracks; Esc cancels a drag.</p><p>Remap shortcuts in Preferences → Shortcuts.</p>";
    QDialog dlg(this);
    dlg.setWindowTitle("Keyboard shortcuts");
    dlg.resize(520, 640);
    auto *v = new QVBoxLayout(&dlg);
    auto *b = new QTextBrowser;
    b->setHtml(html);
    v->addWidget(b);
    dlg.exec();
}

void MainWindow::showPrivacy()
{
    QMessageBox::information(this, "Privacy",
                             "Shyftr Studio works entirely on this computer.\n\n"
                             "• No telemetry, analytics, crash uploads or account.\n"
                             "• Media, transcripts, captions and exports never leave your folders.\n"
                             "• Speech recognition runs locally with models in AI_Models.\n"
                             "• No network connection is needed for editing, transcription or export.");
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Shyftr Studio",
                       QString("<h3>Shyftr Studio %1</h3><p>Offline desktop video editor with local AI subtitles.</p>"
                               "<p>Engines on this computer:<br>%2<br>%3</p>"
                               "<p>Built with Qt %4. FFmpeg and whisper.cpp run as separate local programs.</p>")
                           .arg(QApplication::applicationVersion(),
                                Tools::ffmpeg().isEmpty() ? "FFmpeg: not found" : Tools::ffmpegVersionString().toHtmlEscaped(),
                                Tools::whisper().isEmpty() ? "whisper.cpp: not found" : "whisper.cpp: " + Tools::whisper().toHtmlEscaped(),
                                qVersion()));
}

} // namespace ss
