#include "PreferencesDialog.h"
#include "../captions/Transcription.h"
#include "../core/ProjectStore.h"
#include "../core/Settings.h"
#include "../core/Tools.h"
#include "Widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>
#include <QSpinBox>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace ss {

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Preferences");
    if (QScreen *scr = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
        const QRect a = scr->availableGeometry();
        resize(std::min(820, int(a.width() * 0.9)), std::min(640, int(a.height() * 0.85)));
    }
    auto *v = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    v->addWidget(m_tabs);
    m_tabs->addTab(generalTab(), "General");
    m_tabs->addTab(toolsTab(), "Tools");
    m_modelsIndex = m_tabs->addTab(modelsTab(), "AI Models");
    m_tabs->addTab(performanceTab(), "Performance");
    m_tabs->addTab(shortcutsTab(), "Shortcuts");
    m_tabs->addTab(privacyTab(), "Privacy & Storage");
    m_tabs->addTab(accessibilityTab(), "Accessibility");
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

void PreferencesDialog::showModelsTab()
{
    m_tabs->setCurrentIndex(m_modelsIndex);
}

static void saveAndNotify(PreferencesDialog *d)
{
    Settings::instance().save();
    emit d->settingsChanged();
}

QWidget *PreferencesDialog::generalTab()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    Settings &s = Settings::instance();
    auto *root = new QLineEdit(AppPaths::root());
    root->setReadOnly(true);
    auto *change = new QPushButton("Change…");
    auto *row = new QHBoxLayout;
    row->addWidget(root, 1);
    row->addWidget(change);
    f->addRow("Workspace folder", row);
    auto *note = new QLabel("Contains Projects, AI_Models, User_Templates and User_Preferences. Everything stays on this computer.");
    note->setObjectName("hint");
    note->setWordWrap(true);
    f->addRow("", note);
    auto *autosave = new QSpinBox;
    autosave->setRange(10, 3600);
    autosave->setSuffix(" s");
    autosave->setValue(s.autosaveIntervalSec);
    f->addRow("Autosave every", autosave);
    auto *keep = new QSpinBox;
    keep->setRange(1, 100);
    keep->setValue(s.autosaveKeep);
    f->addRow("Keep autosaves", keep);
    auto *backups = new QSpinBox;
    backups->setRange(0, 50);
    backups->setValue(s.backupKeep);
    f->addRow("Keep save backups", backups);
    auto *undo = new QSpinBox;
    undo->setRange(10, 1000);
    undo->setValue(s.undoLimit);
    f->addRow("Undo steps", undo);
    auto *quick = new QCheckBox("Open projects in Quick Editor mode");
    quick->setChecked(s.startInQuickMode);
    f->addRow("", quick);
    auto *snap = new QCheckBox("Snap clips to edges and playhead (N)");
    snap->setChecked(s.snapping);
    f->addRow("", snap);

    connect(change, &QPushButton::clicked, this, [this, root]() {
        const QString d = QFileDialog::getExistingDirectory(this, "Workspace folder", AppPaths::root());
        if (d.isEmpty())
            return;
        AppPaths::setRoot(d);
        AppPaths::ensureLayout();
        root->setText(d);
        Settings::instance().save();
        QMessageBox::information(this, "Workspace", "New projects and models will use this folder.");
    });
    connect(autosave, &QSpinBox::valueChanged, this, [this](int v) { Settings::instance().autosaveIntervalSec = v; saveAndNotify(this); });
    connect(keep, &QSpinBox::valueChanged, this, [this](int v) { Settings::instance().autosaveKeep = v; saveAndNotify(this); });
    connect(backups, &QSpinBox::valueChanged, this, [this](int v) { Settings::instance().backupKeep = v; saveAndNotify(this); });
    connect(undo, &QSpinBox::valueChanged, this, [this](int v) { Settings::instance().undoLimit = v; saveAndNotify(this); });
    connect(quick, &QCheckBox::toggled, this, [this](bool on) { Settings::instance().startInQuickMode = on; saveAndNotify(this); });
    connect(snap, &QCheckBox::toggled, this, [this](bool on) { Settings::instance().snapping = on; saveAndNotify(this); });
    return w;
}

QWidget *PreferencesDialog::toolsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *intro = new QLabel("Shyftr Studio uses local command-line engines. They are found automatically next to the app, "
                             "on your PATH, or in Homebrew / common install folders. Set a path to override.");
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto *f = new QFormLayout;
    v->addLayout(f);
    struct ToolRow {
        QString label;
        QString *setting;
        std::function<QString()> resolve;
    };
    Settings &s = Settings::instance();
    const QVector<ToolRow> rows{{"FFmpeg", &s.ffmpegPath, &Tools::ffmpeg},
                                {"FFprobe", &s.ffprobePath, &Tools::ffprobe},
                                {"whisper.cpp (whisper-cli)", &s.whisperPath, &Tools::whisper}};
    for (const ToolRow &r : rows) {
        auto *edit = new QLineEdit(*r.setting);
        edit->setPlaceholderText("Automatic: " + (r.resolve().isEmpty() ? QString("not found") : r.resolve()));
        auto *browse = new QPushButton("…");
        auto *h = new QHBoxLayout;
        h->addWidget(edit, 1);
        h->addWidget(browse);
        f->addRow(r.label, h);
        QString *setting = r.setting;
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, setting]() {
            *setting = edit->text().trimmed();
            saveAndNotify(this);
        });
        connect(browse, &QPushButton::clicked, this, [this, edit, setting]() {
            const QString p = QFileDialog::getOpenFileName(this, "Locate executable");
            if (!p.isEmpty()) {
                edit->setText(p);
                *setting = p;
                saveAndNotify(this);
            }
        });
    }
    auto *check = new QPushButton("Check tools");
    auto *result = new QLabel;
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(check);
    v->addWidget(result);
    v->addStretch();
    auto *help = new QLabel("macOS: <code>brew install ffmpeg whisper-cpp</code><br>"
                            "Windows: place ffmpeg.exe, ffprobe.exe and whisper-cli.exe in the app folder, or set paths above.");
    help->setObjectName("hint");
    help->setWordWrap(true);
    v->addWidget(help);
    connect(check, &QPushButton::clicked, this, [result]() {
        QStringList lines;
        const QString ff = Tools::ffmpeg();
        lines << (ff.isEmpty() ? "✗ FFmpeg not found" : "✓ " + Tools::ffmpegVersionString());
        lines << (Tools::ffprobe().isEmpty() ? "✗ FFprobe not found" : "✓ FFprobe: " + Tools::ffprobe());
        lines << (Tools::whisper().isEmpty() ? "✗ whisper-cli not found (needed for automatic captions)"
                                             : "✓ whisper.cpp: " + Tools::whisper());
        if (!ff.isEmpty()) {
            QStringList enc;
            for (const QString &e : QStringList{"libx264", "libx265", "h264_videotoolbox", "hevc_videotoolbox", "h264_nvenc", "h264_qsv", "h264_amf"})
                if (Tools::hasEncoder(e))
                    enc << e;
            lines << "Encoders: " + (enc.isEmpty() ? QString("none of the preferred encoders") : enc.join(", "));
        }
        result->setText(lines.join("\n"));
    });
    return w;
}

QWidget *PreferencesDialog::modelsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *intro = new QLabel(QString("Speech models live in <b>%1</b>. Add a whisper.cpp model file (ggml-*.bin) you have "
                                     "downloaded or received on a drive. Each file's SHA-256 checksum and source are recorded "
                                     "and checked before use.")
                                 .arg(QDir::toNativeSeparators(ModelManager::instance().dir())));
    intro->setWordWrap(true);
    v->addWidget(intro);
    m_models = new QTableWidget(0, 5);
    m_models->setHorizontalHeaderLabels({"Model", "Size", "Status", "SHA-256", "Source"});
    m_models->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_models->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_models->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_models->verticalHeader()->setVisible(false);
    v->addWidget(m_models, 1);
    auto *btns = new QHBoxLayout;
    auto *add = new QPushButton("Add model file…");
    auto *verify = new QPushButton("Verify");
    auto *remove = new QPushButton("Remove");
    auto *open = new QPushButton("Open folder");
    btns->addWidget(add);
    btns->addWidget(verify);
    btns->addWidget(remove);
    btns->addStretch();
    btns->addWidget(open);
    v->addLayout(btns);
    m_modelInfo = new QLabel;
    m_modelInfo->setObjectName("hint");
    m_modelInfo->setWordWrap(true);
    v->addWidget(m_modelInfo);
    QStringList cat;
    for (const auto &c : ModelManager::catalog())
        cat << QString("<b>%1</b> ≈ %2, RAM %3 — %4").arg(c.name, ModelManager::describeSize(c.approxBytes), c.ram, c.note);
    auto *guide = new QLabel("Model guide (disk space):<br>" + cat.join("<br>"));
    guide->setWordWrap(true);
    guide->setObjectName("hint");
    v->addWidget(guide);

    auto selectedFile = [this]() -> QString {
        const int r = m_models->currentRow();
        return r >= 0 ? m_models->item(r, 0)->data(Qt::UserRole).toString() : QString();
    };
    connect(add, &QPushButton::clicked, this, [this]() {
        if (!Policy::load().allowUserModels) {
            QMessageBox::warning(this, "AI Models", "Adding models is disabled by your administrator's policy.");
            return;
        }
        const QString src = QFileDialog::getOpenFileName(this, "Add speech model", QDir::homePath(), "whisper.cpp models (*.bin *.gguf)");
        if (src.isEmpty())
            return;
        const QString expected = QInputDialog::getText(this, "Verify model (optional)",
                                                        "Expected SHA-256 from the model provider (leave empty to record it now):");
        QProgressDialog prog("Copying and verifying model…", "Cancel", 0, 1000, this);
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(0);
        QString err;
        const bool ok = ModelManager::instance().importModel(src, "Added from " + src, expected, &err, [&prog](qint64 d, qint64 t) {
            prog.setValue(t ? int(d * 1000 / t) : 0);
            QApplication::processEvents();
            return !prog.wasCanceled();
        });
        prog.close();
        if (!ok)
            QMessageBox::warning(this, "AI Models", err);
        refreshModels();
        emit modelsChanged();
    });
    connect(verify, &QPushButton::clicked, this, [this, selectedFile]() {
        const QString f = selectedFile();
        if (f.isEmpty())
            return;
        QProgressDialog prog("Verifying checksum…", "Cancel", 0, 1000, this);
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(0);
        QString err;
        const bool ok = ModelManager::instance().verify(f, &err, [&prog](qint64 d, qint64 t) {
            prog.setValue(t ? int(d * 1000 / t) : 0);
            QApplication::processEvents();
            return !prog.wasCanceled();
        });
        prog.close();
        if (ok)
            QMessageBox::information(this, "AI Models", "The model checksum is recorded and matches.");
        else
            QMessageBox::warning(this, "AI Models", err);
        refreshModels();
    });
    connect(remove, &QPushButton::clicked, this, [this, selectedFile]() {
        const QString f = selectedFile();
        if (f.isEmpty() || QMessageBox::question(this, "AI Models", "Delete " + f + " from AI_Models?") != QMessageBox::Yes)
            return;
        QString err;
        if (!ModelManager::instance().remove(f, &err))
            QMessageBox::warning(this, "AI Models", err);
        refreshModels();
        emit modelsChanged();
    });
    connect(open, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(ModelManager::instance().dir())); });
    refreshModels();
    return w;
}

void PreferencesDialog::refreshModels()
{
    const auto models = ModelManager::instance().scan();
    m_models->setRowCount(models.size());
    qint64 total = 0;
    for (int i = 0; i < models.size(); ++i) {
        const ModelInfo &m = models[i];
        total += m.size;
        auto *name = new QTableWidgetItem(m.name + (m.isVad ? " (speech detection)" : ""));
        name->setData(Qt::UserRole, m.file);
        m_models->setItem(i, 0, name);
        m_models->setItem(i, 1, new QTableWidgetItem(ModelManager::describeSize(m.size)));
        auto *st = new QTableWidgetItem(m.status);
        st->setForeground(m.status == "verified" ? QColor("#69DB7C")
                         : (m.status == "changed" || m.status == "invalid") ? QColor("#FF6B6B") : QColor("#FFA94D"));
        if (m.status == "invalid") {
            QString why;
            ModelManager::checkModelFile(m.path, &why);
            st->setText("invalid — not a model");
            st->setToolTip(why);
        }
        m_models->setItem(i, 2, st);
        m_models->setItem(i, 3, new QTableWidgetItem(m.sha256.left(16) + (m.sha256.isEmpty() ? "" : "…")));
        m_models->item(i, 3)->setToolTip(m.sha256);
        m_models->setItem(i, 4, new QTableWidgetItem(m.provenance));
    }
    m_models->resizeColumnsToContents();
    QStorageInfo si(ModelManager::instance().dir());
    m_modelInfo->setText(QString("%1 model(s), %2 on disk · %3 free")
                             .arg(models.size())
                             .arg(ModelManager::describeSize(total), ModelManager::describeSize(si.bytesAvailable())));
}

QWidget *PreferencesDialog::performanceTab()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    Settings &s = Settings::instance();
    auto *enc = new QComboBox;
    enc->addItem("Automatic (hardware when available)", "auto");
    enc->addItem("Prefer hardware (VideoToolbox / NVENC / QSV / AMF)", "hardware");
    enc->addItem("Software only (libx264 / libx265)", "software");
    enc->setCurrentIndex(std::max(0, enc->findData(s.encoderPreference)));
    f->addRow("Export encoder", enc);
    auto *gpu = new QCheckBox("Use GPU for speech recognition (Metal / CUDA when available)");
    gpu->setChecked(s.aiUseGpu);
    f->addRow("", gpu);
    auto *threads = new QSpinBox;
    threads->setRange(0, 64);
    threads->setSpecialValueText("Automatic");
    threads->setValue(s.aiThreads);
    f->addRow("AI threads", threads);
    auto *prev = new QComboBox;
    prev->addItem("Full (up to 1920 px)", 1920);
    prev->addItem("Half (960 px)", 960);
    prev->addItem("Quarter (480 px) — slower computers", 480);
    prev->setCurrentIndex(s.previewMaxWidth >= 1920 ? 0 : s.previewMaxWidth >= 960 ? 1 : 2);
    f->addRow("Preview resolution", prev);
    auto *note = new QLabel(QString("This computer: %1 logical CPU cores. Lower preview resolution keeps scrubbing smooth "
                                    "on 8 GB machines; export always renders at full quality.")
                                .arg(QThread::idealThreadCount()));
    note->setWordWrap(true);
    note->setObjectName("hint");
    f->addRow("", note);
    connect(enc, &QComboBox::currentIndexChanged, this, [this, enc](int) { Settings::instance().encoderPreference = enc->currentData().toString(); saveAndNotify(this); });
    connect(gpu, &QCheckBox::toggled, this, [this](bool on) { Settings::instance().aiUseGpu = on; saveAndNotify(this); });
    connect(threads, &QSpinBox::valueChanged, this, [this](int v) { Settings::instance().aiThreads = v; saveAndNotify(this); });
    connect(prev, &QComboBox::currentIndexChanged, this, [this, prev](int) { Settings::instance().previewMaxWidth = prev->currentData().toInt(); saveAndNotify(this); });
    return w;
}

QWidget *PreferencesDialog::shortcutsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    m_shortcuts = new QTableWidget(0, 2);
    m_shortcuts->setHorizontalHeaderLabels({"Action", "Shortcut"});
    m_shortcuts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcuts->verticalHeader()->setVisible(false);
    v->addWidget(m_shortcuts, 1);
    auto *conflicts = new QLabel;
    conflicts->setObjectName("error");
    conflicts->setWordWrap(true);
    v->addWidget(conflicts);
    auto *reset = new QPushButton("Reset all to defaults");
    v->addWidget(reset);
    auto updateConflicts = [conflicts]() { conflicts->setText(Shortcuts::instance().conflicts().join("\n")); };
    connect(reset, &QPushButton::clicked, this, [this, updateConflicts]() {
        Shortcuts::instance().resetAll();
        Shortcuts::instance().save();
        refreshShortcuts();
        updateConflicts();
        emit settingsChanged();
    });
    refreshShortcuts();
    updateConflicts();
    for (int r = 0; r < m_shortcuts->rowCount(); ++r) {
        auto *edit = qobject_cast<QKeySequenceEdit *>(m_shortcuts->cellWidget(r, 1));
        const QString id = m_shortcuts->item(r, 0)->data(Qt::UserRole).toString();
        connect(edit, &QKeySequenceEdit::editingFinished, this, [this, edit, id, updateConflicts]() {
            Shortcuts::instance().setKey(id, edit->keySequence());
            Shortcuts::instance().save();
            updateConflicts();
            emit settingsChanged();
        });
    }
    return w;
}

void PreferencesDialog::refreshShortcuts()
{
    const auto entries = Shortcuts::instance().entries();
    m_shortcuts->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        auto *it = new QTableWidgetItem(entries[i].label);
        it->setData(Qt::UserRole, entries[i].id);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        m_shortcuts->setItem(i, 0, it);
        auto *edit = qobject_cast<QKeySequenceEdit *>(m_shortcuts->cellWidget(i, 1));
        if (!edit) {
            edit = new QKeySequenceEdit;
            m_shortcuts->setCellWidget(i, 1, edit);
        }
        edit->setKeySequence(entries[i].key);
    }
}

QWidget *PreferencesDialog::privacyTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *stmt = new QLabel(
        "<b>Privacy statement</b><br>"
        "• Shyftr Studio has no telemetry, analytics or crash uploading. Nothing is sent anywhere.<br>"
        "• Media, transcripts, captions and exports stay in folders you choose on this computer.<br>"
        "• Speech recognition runs locally with the model files in AI_Models.<br>"
        "• No account, sign-in or licence server is required to edit.<br>"
        "• Logs (exports/logs inside each project) stay local; you decide whether to share them.");
    stmt->setWordWrap(true);
    v->addWidget(stmt);
    v->addWidget(hline());
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto *keep = new QCheckBox("Keep autosaves after a clean close (for extra safety)");
    keep->setChecked(Settings::instance().keepAutosavesOnClose);
    f->addRow("", keep);
    connect(keep, &QCheckBox::toggled, this, [this](bool on) { Settings::instance().keepAutosavesOnClose = on; saveAndNotify(this); });
    auto *cacheInfo = new QLabel;
    cacheInfo->setObjectName("hint");
    auto *clear = new QPushButton("Clear thumbnails, waveforms and transcription work files in all projects");
    f->addRow("Caches", clear);
    f->addRow("", cacheInfo);
    auto computeCache = [cacheInfo]() {
        qint64 total = 0;
        QDir pd(AppPaths::projectsDir());
        for (const QString &proj : pd.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QString &sub : QStringList{"thumbnails", "waveforms", "proxies", "subtitles/work"})
                total += ProjectStore::folderSize(pd.filePath(proj + "/" + sub));
        cacheInfo->setText(QString("Cache size in %1: %2").arg(AppPaths::projectsDir(), ModelManager::describeSize(total)));
    };
    computeCache();
    connect(clear, &QPushButton::clicked, this, [this, computeCache]() {
        if (QMessageBox::question(this, "Clear caches", "Remove cached thumbnails, waveforms and transcription work files? "
                                                        "They are regenerated when needed.") != QMessageBox::Yes)
            return;
        QDir pd(AppPaths::projectsDir());
        for (const QString &proj : pd.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QString &sub : QStringList{"thumbnails", "waveforms", "proxies", "subtitles/work"}) {
                QDir d(pd.filePath(proj + "/" + sub));
                d.removeRecursively();
                QDir().mkpath(d.path());
            }
        computeCache();
    });
    const Policy pol = Policy::load();
    auto *policy = new QLabel(QString("Administrator policy (User_Preferences/policy.json): cache retention %1, user models %2.")
                                  .arg(pol.cacheRetentionDays > 0 ? QString::number(pol.cacheRetentionDays) + " days" : QString("unlimited"),
                                       pol.allowUserModels ? "allowed" : "blocked"));
    policy->setObjectName("hint");
    policy->setWordWrap(true);
    v->addWidget(policy);
    v->addStretch();
    return w;
}

QWidget *PreferencesDialog::accessibilityTab()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    auto *hc = new QCheckBox("High-contrast theme");
    hc->setChecked(Settings::instance().highContrast);
    f->addRow("", hc);
    auto *scale = new QDoubleSpinBox;
    scale->setRange(0.75, 2.0);
    scale->setSingleStep(0.05);
    scale->setValue(Settings::instance().uiScale);
    scale->setSuffix("×");
    f->addRow("Text size", scale);
    auto *note = new QLabel("All controls are reachable by keyboard (Tab / Shift+Tab); timeline editing is available "
                            "through menu commands and remappable shortcuts.");
    note->setWordWrap(true);
    note->setObjectName("hint");
    f->addRow("", note);
    connect(hc, &QCheckBox::toggled, this, [this](bool on) { Settings::instance().highContrast = on; saveAndNotify(this); });
    connect(scale, &QDoubleSpinBox::valueChanged, this, [this](double v) { Settings::instance().uiScale = v; saveAndNotify(this); });
    return w;
}

} // namespace ss
