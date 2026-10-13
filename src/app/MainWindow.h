#pragma once

#include "../core/Document.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class QStackedWidget;
class QDockWidget;
class QLabel;
class QLockFile;
class QAction;

namespace ss {

class MediaCache;
class PreviewEngine;
class WelcomePage;
class ViewerWidget;
class TimelineWidget;
class MediaBinWidget;
class InspectorWidget;
class SubtitlePanel;
class EffectsBrowser;
class QuickPanel;
class ExportDialog;
class PreferencesDialog;
class ToolsController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    bool openProject(const QString &dir, bool recover = false);
    void setMode(bool quick) { setQuickMode(quick); }
    void showSubtitleTab(int index);

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    // setup
    void buildUi();
    void buildActions();
    void buildMenus();
    QAction *act(const QString &id, const QString &label, const QKeySequence &key, const std::function<void()> &fn,
                 bool editingCommand = false);
    void applyShortcuts();
    void applyTheme();

    // project lifecycle
    void createProject(const QString &name, const QString &parentDir, const CanvasSettings &canvas);
    void openProjectDialog();
    bool saveProject();
    bool maybeSave();
    bool closeProject();
    void packageProject();
    void collectMedia();
    void relinkMedia();
    void autosaveTick();
    void updateTitle();
    void showWelcome();
    void showEditor();
    void setQuickMode(bool quick);
    void applyDockProportions(bool quick);
    void fitToScreen();
    void editCaptionInViewer(const QRect &anchor = QRect());
    void editTitleInViewer(const QString &clipId, const QRect &anchor);

    // editing
    void importDialog();
    void importFiles(const QStringList &paths, TimeUs at = -1, const QString &trackId = {}, bool asMusic = false);
    void addMusic();
    void recordVoiceOver();
    void openExport(const QString &presetId = {});
    void openPreferences(bool models = false);
    void splitAtPlayhead(bool allTracks);
    void deleteSelection(bool ripple);
    void copySelection(bool cut);
    void paste();
    void jumpEdit(int dir);
    bool isTypingInText() const;
    void showShortcutsHelp();
    void showAbout();
    void showPrivacy();

    Document m_doc;
    MediaCache *m_cache = nullptr;
    PreviewEngine *m_engine = nullptr;
    QStackedWidget *m_stack = nullptr;
    WelcomePage *m_welcome = nullptr;
    QWidget *m_editorCentral = nullptr;
    ViewerWidget *m_viewer = nullptr;
    TimelineWidget *m_timeline = nullptr;
    MediaBinWidget *m_mediaBin = nullptr;
    InspectorWidget *m_inspector = nullptr;
    SubtitlePanel *m_subtitles = nullptr;
    EffectsBrowser *m_effects = nullptr;
    QuickPanel *m_quick = nullptr;
    QDockWidget *m_mediaDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QDockWidget *m_subtitleDock = nullptr;
    QDockWidget *m_effectsDock = nullptr;
    QDockWidget *m_timelineDock = nullptr;
    QDockWidget *m_quickDock = nullptr;
    ToolsController *m_tools = nullptr;
    QPointer<ExportDialog> m_exportDialog;
    QLockFile *m_lock = nullptr;
    QTimer m_autosave;
    int m_lastAutosaveCounter = -1;
    bool m_quickMode = true;
    bool m_switchingMode = false;
    QHash<QString, QAction *> m_actions;
    QLabel *m_saveState = nullptr;
    QLabel *m_offline = nullptr;
    struct ClipboardItem {
        Clip clip;
        TrackKind kind;
        int trackOrdinal;
        TimeUs offset;
    };
    QVector<ClipboardItem> m_clipboard;
};

} // namespace ss
