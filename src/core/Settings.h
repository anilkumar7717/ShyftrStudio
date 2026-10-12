#pragma once

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QString>
#include <QStringList>

namespace ss {

// Workspace layout (all local):
//   <root>/Projects/          one folder per project
//   <root>/AI_Models/         speech model packs + manifest.json
//   <root>/User_Templates/    saved caption styles / brand presets
//   <root>/User_Preferences/  settings.json, shortcuts.json, recent.json, policy.json
class AppPaths {
public:
    static QString root();
    static void setRoot(const QString &path); // persisted in native settings
    static QString projectsDir();
    static QString modelsDir();
    static QString userTemplatesDir();
    static QString preferencesDir();
    static bool ensureLayout(QString *error = nullptr);
    static QString defaultRoot();
};

struct Settings {
    // General
    int autosaveIntervalSec = 60;
    int autosaveKeep = 10;
    int backupKeep = 5;
    int undoLimit = 100;
    bool startInQuickMode = true;
    bool snapping = true;
    // Tools
    QString ffmpegPath;
    QString ffprobePath;
    QString whisperPath;
    // Performance
    QString encoderPreference = "auto"; // auto | hardware | software
    int previewMaxWidth = 960;
    int aiThreads = 0;       // 0 = automatic
    bool aiUseGpu = true;
    // AI
    QString defaultLanguage = "auto";
    QString defaultModel;
    bool useVad = false;
    bool refineWordTiming = true;
    // Accessibility
    bool highContrast = false;
    double uiScale = 1.0;
    // Privacy: there is no telemetry. These flags only control local behaviour.
    bool keepAutosavesOnClose = true;
    QString cacheRoot; // empty => per-project folders

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &o);

    static Settings &instance();
    bool load();
    bool save() const;
};

// Remappable keyboard shortcuts (User_Preferences/shortcuts.json).
class Shortcuts {
public:
    struct Entry {
        QString id;
        QString label;
        QKeySequence defaultKey;
        QKeySequence key;
    };
    static Shortcuts &instance();
    void registerAction(const QString &id, const QString &label, const QKeySequence &def);
    QKeySequence key(const QString &id) const;
    void setKey(const QString &id, const QKeySequence &k);
    void resetAll();
    QVector<Entry> entries() const;
    QStringList conflicts() const;
    bool load();
    bool save() const;

private:
    QVector<Entry> m_entries;
    QHash<QString, int> m_index;
    QHash<QString, QString> m_loaded; // id -> portable text loaded from disk
};

// Recently opened projects (User_Preferences/recent.json).
class RecentProjects {
public:
    static QStringList list();
    static void add(const QString &projectDir);
    static void remove(const QString &projectDir);
};

// Optional administrator policy (User_Preferences/policy.json): default export settings,
// cache retention and other locked values for managed deployments.
struct Policy {
    int cacheRetentionDays = 0; // 0 = keep
    QJsonObject defaultExport;
    bool allowUserModels = true;
    static Policy load();
};

bool writeJsonAtomically(const QString &path, const QJsonObject &obj, QString *error = nullptr);
bool readJson(const QString &path, QJsonObject &obj, QString *error = nullptr);

} // namespace ss
