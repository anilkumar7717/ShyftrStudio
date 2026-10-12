#include "ProjectStore.h"
#include "ProjectSerializer.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <algorithm>

namespace ss {

QStringList ProjectStore::subfolders()
{
    return {"media", "subtitles", "templates", "thumbnails", "proxies", "waveforms", "autosaves", "exports", "backups"};
}

QString ProjectStore::sanitizeName(const QString &name)
{
    QString s = name.trimmed();
    s.replace(QRegularExpression(R"([\\/:*?"<>|\x00-\x1f])"), "_");
    if (s.isEmpty())
        s = "Untitled";
    return s.left(120);
}

bool ProjectStore::createProject(const QString &projectDir, const Project &p, QString *error)
{
    QDir d(projectDir);
    if (d.exists() && QFileInfo::exists(d.filePath("project.json"))) {
        if (error)
            *error = "A project already exists in " + projectDir;
        return false;
    }
    for (const QString &sub : subfolders())
        if (!d.mkpath(sub)) {
            if (error)
                *error = "Could not create " + d.filePath(sub);
            return false;
        }
    return save(p, projectDir, error, 0);
}

QString ProjectStore::relativeTo(const QString &projectDir, const QString &absPath)
{
    return QDir(projectDir).relativeFilePath(absPath);
}

void ProjectStore::refreshMediaPaths(Project &p, const QString &projectDir, LoadReport *report)
{
    const QDir dir(projectDir);
    for (auto &m : p.media) {
        m.missing = false;
        if (!m.path.isEmpty() && QFileInfo::exists(m.path)) {
            m.relativePath = relativeTo(projectDir, m.path);
            continue;
        }
        if (!m.relativePath.isEmpty()) {
            const QString candidate = QDir::cleanPath(dir.filePath(m.relativePath));
            if (QFileInfo::exists(candidate)) {
                m.path = candidate;
                if (report)
                    report->resolvedViaRelative << m.id;
                continue;
            }
        }
        // collected copy inside media/?
        const QString collected = dir.filePath("media/" + QFileInfo(m.path).fileName());
        if (!m.path.isEmpty() && QFileInfo::exists(collected)) {
            m.path = collected;
            m.relativePath = relativeTo(projectDir, collected);
            if (report)
                report->resolvedViaRelative << m.id;
            continue;
        }
        m.missing = true;
        if (report)
            report->missingMediaIds << m.id;
    }
}

bool ProjectStore::loadFile(const QString &file, Project &out, LoadReport *report, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QString("Could not open %1: %2").arg(file, f.errorString());
        return false;
    }
    int migrated = 0;
    if (!ProjectSerializer::fromBytes(f.readAll(), out, error, &migrated))
        return false;
    if (report)
        report->migratedFromVersion = migrated;
    return true;
}

bool ProjectStore::load(const QString &projectDirOrFile, Project &out, LoadReport *report, QString *error)
{
    QFileInfo fi(projectDirOrFile);
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    const QString file = fi.isDir() ? dir + "/project.json" : fi.absoluteFilePath();
    QString err;
    if (!loadFile(file, out, report, &err)) {
        // Fall back to the newest valid backup so a damaged project.json never loses work.
        for (const QString &b : backups(dir)) {
            QString e2;
            if (loadFile(b, out, report, &e2)) {
                if (report)
                    report->warning = QString("project.json could not be read (%1). The most recent backup (%2) was "
                                              "loaded instead.")
                                          .arg(err, QFileInfo(b).fileName());
                refreshMediaPaths(out, dir, report);
                return true;
            }
        }
        if (error)
            *error = err;
        return false;
    }
    // ensure folder layout exists for projects created elsewhere
    for (const QString &sub : subfolders())
        QDir(dir).mkpath(sub);
    refreshMediaPaths(out, dir, report);
    return true;
}

QStringList ProjectStore::backups(const QString &projectDir)
{
    QDir d(projectDir + "/backups");
    QStringList out;
    const auto list = d.entryInfoList({"project-*.json"}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : list)
        out << fi.absoluteFilePath();
    return out;
}

QStringList ProjectStore::autosaves(const QString &projectDir)
{
    QDir d(projectDir + "/autosaves");
    QStringList out;
    const auto list = d.entryInfoList({"autosave-*.json"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &fi : list)
        out << fi.absoluteFilePath();
    return out;
}

bool ProjectStore::save(const Project &pIn, const QString &projectDir, QString *error, int backupKeep)
{
    Project p = pIn;
    for (auto &m : p.media)
        if (!m.path.isEmpty())
            m.relativePath = relativeTo(projectDir, m.path);

    const QStringList problems = ProjectSerializer::validate(p);
    if (!problems.isEmpty()) {
        if (error)
            *error = "Refusing to save an invalid project:\n- " + problems.join("\n- ");
        return false;
    }
    const QByteArray bytes = ProjectSerializer::toBytes(p);

    // Validate the exact bytes we are about to write by parsing them back.
    Project check;
    QString perr;
    if (!ProjectSerializer::fromBytes(bytes, check, &perr)) {
        if (error)
            *error = "Serialisation self-check failed: " + perr;
        return false;
    }

    QDir dir(projectDir);
    dir.mkpath(".");
    const QString target = dir.filePath("project.json");

    // Disk space guard (needs room for temp file + backup)
    QStorageInfo si(projectDir);
    if (si.isValid() && si.bytesAvailable() >= 0 && si.bytesAvailable() < bytes.size() * 3 + 1024 * 1024) {
        if (error)
            *error = "Not enough free disk space to save the project safely.";
        return false;
    }

    // Rotate backups: copy the current valid project.json before replacing it.
    if (backupKeep > 0 && QFileInfo::exists(target)) {
        dir.mkpath("backups");
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
        QFile::copy(target, dir.filePath("backups/project-" + stamp + ".json"));
        const QStringList b = backups(projectDir);
        for (int i = backupKeep; i < b.size(); ++i)
            QFile::remove(b[i]);
    }

    QSaveFile f(target);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QString("Could not write %1: %2").arg(target, f.errorString());
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        if (error)
            *error = "Write failed: " + f.errorString();
        return false;
    }
    if (!f.commit()) { // atomic rename; the old file stays intact on failure
        if (error)
            *error = "Could not finalise the save: " + f.errorString();
        return false;
    }

    // Human-readable mirrors for interchange (non-authoritative; failures are not fatal).
    dir.mkpath("subtitles");
    for (const auto &t : p.transcripts) {
        QSaveFile tf(dir.filePath("subtitles/transcript-" + t.id + ".json"));
        if (tf.open(QIODevice::WriteOnly)) {
            tf.write(QJsonDocument(ProjectSerializer::transcriptToJson(t)).toJson());
            tf.commit();
        }
    }
    return true;
}

QString ProjectStore::writeAutosave(const Project &p, const QString &projectDir, int keep, QString *error)
{
    QDir dir(projectDir);
    dir.mkpath("autosaves");
    const QString name = "autosave-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".json";
    const QString path = dir.filePath("autosaves/" + name);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    f.write(ProjectSerializer::toBytes(p, true));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return {};
    }
    const QStringList all = autosaves(projectDir);
    for (int i = std::max(1, keep); i < all.size(); ++i)
        QFile::remove(all[i]);
    return path;
}

static QString sessionFile(const QString &projectDir)
{
    return projectDir + "/autosaves/session.json";
}

void ProjectStore::markSessionOpen(const QString &projectDir)
{
    QJsonObject o;
    o["cleanShutdown"] = false;
    o["pid"] = qint64(QCoreApplication::applicationPid());
    o["openedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    writeJsonAtomically(sessionFile(projectDir), o);
}

void ProjectStore::markSessionClosed(const QString &projectDir)
{
    QJsonObject o;
    readJson(sessionFile(projectDir), o);
    o["cleanShutdown"] = true;
    o["closedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    writeJsonAtomically(sessionFile(projectDir), o);
}

RecoveryInfo ProjectStore::recoveryInfo(const QString &projectDir)
{
    RecoveryInfo r;
    QJsonObject o;
    if (readJson(sessionFile(projectDir), o))
        r.uncleanShutdown = !o.value("cleanShutdown").toBool(true);
    const QStringList a = autosaves(projectDir);
    if (!a.isEmpty()) {
        r.autosavePath = a.first();
        r.autosaveTime = QFileInfo(a.first()).lastModified();
    }
    r.projectTime = QFileInfo(projectDir + "/project.json").lastModified();
    return r;
}

QHash<QString, QString> ProjectStore::searchForMissing(const Project &p, const QString &folder, int maxDepth)
{
    QHash<QString, QString> found;
    QHash<QString, QVector<const MediaAsset *>> byName;
    for (const auto &m : p.media)
        if (m.missing)
            byName[QFileInfo(m.path).fileName().toLower()] << &m;
    if (byName.isEmpty())
        return found;
    const int baseDepth = QDir::cleanPath(folder).count('/');
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QDir::cleanPath(path).count('/') - baseDepth > maxDepth)
            continue;
        const QFileInfo fi = it.fileInfo();
        const auto cands = byName.value(fi.fileName().toLower());
        for (const MediaAsset *m : cands) {
            if (found.contains(m->id))
                continue;
            if (m->fileSize > 0 && fi.size() != m->fileSize)
                continue;
            found[m->id] = fi.absoluteFilePath();
        }
        if (found.size() == p.media.size())
            break;
    }
    return found;
}

int ProjectStore::applyRelink(Project &p, const QHash<QString, QString> &mediaIdToPath, const QString &projectDir)
{
    int n = 0;
    for (auto &m : p.media) {
        auto it = mediaIdToPath.find(m.id);
        if (it == mediaIdToPath.end())
            continue;
        m.path = it.value();
        m.relativePath = relativeTo(projectDir, m.path);
        m.missing = !QFileInfo::exists(m.path);
        if (!m.missing)
            ++n;
    }
    return n;
}

qint64 ProjectStore::estimateCollectBytes(const Project &p, const QString &projectDir)
{
    const QString mediaDir = QDir(projectDir).absoluteFilePath("media");
    qint64 total = 0;
    for (const auto &m : p.media) {
        if (m.missing || QFileInfo(m.path).absolutePath() == mediaDir)
            continue;
        total += QFileInfo(m.path).size();
    }
    return total;
}

static bool copyWithUniqueName(const QString &src, const QString &destDir, QString *outPath)
{
    QFileInfo fi(src);
    QString dest = destDir + "/" + fi.fileName();
    int n = 1;
    while (QFileInfo::exists(dest)) {
        if (QFileInfo(dest).size() == fi.size()) { // already collected
            *outPath = dest;
            return true;
        }
        dest = QString("%1/%2-%3.%4").arg(destDir, fi.completeBaseName()).arg(n++).arg(fi.suffix());
    }
    if (!QFile::copy(src, dest))
        return false;
    *outPath = dest;
    return true;
}

bool ProjectStore::collectMedia(Project &p, const QString &projectDir, QString *error,
                                const std::function<bool(int, int)> &progress)
{
    const QString mediaDir = QDir(projectDir).absoluteFilePath("media");
    QDir().mkpath(mediaDir);
    const qint64 need = estimateCollectBytes(p, projectDir);
    QStorageInfo si(projectDir);
    if (si.isValid() && si.bytesAvailable() >= 0 && si.bytesAvailable() < need + 64 * 1024 * 1024) {
        if (error)
            *error = QString("Collecting media needs %1 MB but only %2 MB are free.")
                         .arg(need / (1024 * 1024))
                         .arg(si.bytesAvailable() / (1024 * 1024));
        return false;
    }
    int done = 0;
    const int total = p.media.size();
    for (auto &m : p.media) {
        if (progress && !progress(done, total)) {
            if (error)
                *error = "Cancelled";
            return false;
        }
        ++done;
        if (m.missing || QFileInfo(m.path).absolutePath() == mediaDir)
            continue;
        QString dest;
        if (!copyWithUniqueName(m.path, mediaDir, &dest)) {
            if (error)
                *error = "Could not copy " + m.path;
            return false;
        }
        m.path = dest;
        m.relativePath = relativeTo(projectDir, dest);
    }
    if (progress)
        progress(total, total);
    return true;
}

bool ProjectStore::packageProject(const Project &pIn, const QString &projectDir, const QString &destDir, QString *error,
                                  const std::function<bool(int, int)> &progress)
{
    Project p = pIn;
    const QString pkg = destDir + "/" + sanitizeName(p.name) + ".shyftr";
    if (QFileInfo::exists(pkg + "/project.json")) {
        if (error)
            *error = "A package already exists at " + pkg;
        return false;
    }
    for (const QString &sub : subfolders())
        QDir().mkpath(pkg + "/" + sub);
    int done = 0;
    const int total = p.media.size();
    for (auto &m : p.media) {
        if (progress && !progress(done, total)) {
            if (error)
                *error = "Cancelled";
            return false;
        }
        ++done;
        if (m.missing)
            continue;
        QString dest;
        if (!copyWithUniqueName(m.path, pkg + "/media", &dest)) {
            if (error)
                *error = "Could not copy " + m.path;
            return false;
        }
        m.path = dest;
    }
    // project-specific templates
    QDir tdir(projectDir + "/templates");
    for (const QString &f : tdir.entryList({"*.json"}, QDir::Files))
        QFile::copy(tdir.filePath(f), pkg + "/templates/" + f);
    if (progress)
        progress(total, total);
    return save(p, pkg, error, 0);
}

qint64 ProjectStore::folderSize(const QString &dir)
{
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

} // namespace ss
