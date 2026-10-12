#pragma once

#include "Model.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <functional>

namespace ss {

struct LoadReport {
    int migratedFromVersion = 0;   // non-zero when an explicit schema migration ran
    QStringList missingMediaIds;   // media whose files could not be found
    QStringList resolvedViaRelative; // media found through the project-relative path
    QString warning;
};

struct RecoveryInfo {
    bool uncleanShutdown = false;
    QString autosavePath;
    QDateTime autosaveTime;
    QDateTime projectTime;
    bool autosaveIsNewer() const { return !autosavePath.isEmpty() && autosaveTime > projectTime; }
};

// Local, file-based persistence. No database: project.json is written atomically
// (temp file + validation + rename), with rotating backups and rolling autosaves.
class ProjectStore {
public:
    // Folder layout inside a project directory
    static QStringList subfolders(); // media, subtitles, templates, thumbnails, proxies, waveforms, autosaves, exports, backups

    static bool createProject(const QString &projectDir, const Project &p, QString *error);
    static bool load(const QString &projectDirOrFile, Project &out, LoadReport *report, QString *error);
    static bool loadFile(const QString &file, Project &out, LoadReport *report, QString *error);

    // Atomic save with validation and backup rotation.
    static bool save(const Project &p, const QString &projectDir, QString *error, int backupKeep = 5);

    static QString writeAutosave(const Project &p, const QString &projectDir, int keep, QString *error = nullptr);
    static QStringList autosaves(const QString &projectDir); // newest first
    static QStringList backups(const QString &projectDir);   // newest first

    // Session marker used for crash detection.
    static void markSessionOpen(const QString &projectDir);
    static void markSessionClosed(const QString &projectDir);
    static RecoveryInfo recoveryInfo(const QString &projectDir);

    // Media path handling
    static void refreshMediaPaths(Project &p, const QString &projectDir, LoadReport *report);
    static QString relativeTo(const QString &projectDir, const QString &absPath);
    // Search a folder (recursively) for missing media by file name (+ size when known).
    static QHash<QString, QString> searchForMissing(const Project &p, const QString &folder, int maxDepth = 6);
    static int applyRelink(Project &p, const QHash<QString, QString> &mediaIdToPath, const QString &projectDir);

    // Copies all referenced media into <project>/media (portable project).
    static qint64 estimateCollectBytes(const Project &p, const QString &projectDir);
    static bool collectMedia(Project &p, const QString &projectDir, QString *error,
                             const std::function<bool(int done, int total)> &progress = {});
    // Writes a self-contained copy of the project (project.json + media + subtitles) to destDir.
    static bool packageProject(const Project &p, const QString &projectDir, const QString &destDir, QString *error,
                               const std::function<bool(int done, int total)> &progress = {});

    static QString sanitizeName(const QString &name);
    static qint64 folderSize(const QString &dir);
};

} // namespace ss
