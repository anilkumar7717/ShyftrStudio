#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace ss {

bool writeJsonAtomically(const QString &path, const QJsonObject &obj, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool readJson(const QString &path, QJsonObject &obj, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    obj = d.object();
    return true;
}

// ---------------------------------------------------------------- AppPaths

static QString g_rootOverride;

QString AppPaths::defaultRoot()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty())
        docs = QDir::homePath();
    return docs + "/ShyftrStudio";
}

QString AppPaths::root()
{
    if (!g_rootOverride.isEmpty())
        return g_rootOverride;
    const QByteArray env = qgetenv("SHYFTR_STUDIO_ROOT");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    QSettings s;
    return s.value("workspaceRoot", defaultRoot()).toString();
}

void AppPaths::setRoot(const QString &path)
{
    g_rootOverride = path;
    QSettings s;
    s.setValue("workspaceRoot", path);
}

QString AppPaths::projectsDir() { return root() + "/Projects"; }
QString AppPaths::modelsDir() { return root() + "/AI_Models"; }
QString AppPaths::userTemplatesDir() { return root() + "/User_Templates"; }
QString AppPaths::preferencesDir() { return root() + "/User_Preferences"; }

bool AppPaths::ensureLayout(QString *error)
{
    for (const QString &d : {projectsDir(), modelsDir(), userTemplatesDir(), preferencesDir()})
        if (!QDir().mkpath(d)) {
            if (error)
                *error = "Could not create folder " + d;
            return false;
        }
    return true;
}

// ---------------------------------------------------------------- Settings

QJsonObject Settings::toJson() const
{
    QJsonObject o;
    o["autosaveIntervalSec"] = autosaveIntervalSec;
    o["autosaveKeep"] = autosaveKeep;
    o["backupKeep"] = backupKeep;
    o["undoLimit"] = undoLimit;
    o["startInQuickMode"] = startInQuickMode;
    o["snapping"] = snapping;
    o["ffmpegPath"] = ffmpegPath;
    o["ffprobePath"] = ffprobePath;
    o["whisperPath"] = whisperPath;
    o["encoderPreference"] = encoderPreference;
    o["previewMaxWidth"] = previewMaxWidth;
    o["aiThreads"] = aiThreads;
    o["aiUseGpu"] = aiUseGpu;
    o["defaultLanguage"] = defaultLanguage;
    o["defaultModel"] = defaultModel;
    o["useVad"] = useVad;
    o["refineWordTiming"] = refineWordTiming;
    o["highContrast"] = highContrast;
    o["uiScale"] = uiScale;
    o["keepAutosavesOnClose"] = keepAutosavesOnClose;
    o["cacheRoot"] = cacheRoot;
    o["telemetry"] = false; // documented: the app has no telemetry
    return o;
}

Settings Settings::fromJson(const QJsonObject &o)
{
    Settings s;
    s.autosaveIntervalSec = std::clamp(o.value("autosaveIntervalSec").toInt(s.autosaveIntervalSec), 10, 3600);
    s.autosaveKeep = std::clamp(o.value("autosaveKeep").toInt(s.autosaveKeep), 1, 100);
    s.backupKeep = std::clamp(o.value("backupKeep").toInt(s.backupKeep), 0, 50);
    s.undoLimit = std::clamp(o.value("undoLimit").toInt(s.undoLimit), 10, 1000);
    s.startInQuickMode = o.value("startInQuickMode").toBool(s.startInQuickMode);
    s.snapping = o.value("snapping").toBool(s.snapping);
    s.ffmpegPath = o.value("ffmpegPath").toString();
    s.ffprobePath = o.value("ffprobePath").toString();
    s.whisperPath = o.value("whisperPath").toString();
    s.encoderPreference = o.value("encoderPreference").toString(s.encoderPreference);
    s.previewMaxWidth = std::clamp(o.value("previewMaxWidth").toInt(s.previewMaxWidth), 320, 3840);
    s.aiThreads = std::clamp(o.value("aiThreads").toInt(0), 0, 64);
    s.aiUseGpu = o.value("aiUseGpu").toBool(true);
    s.defaultLanguage = o.value("defaultLanguage").toString(s.defaultLanguage);
    s.defaultModel = o.value("defaultModel").toString();
    s.useVad = o.value("useVad").toBool(false);
    s.refineWordTiming = o.value("refineWordTiming").toBool(true);
    s.highContrast = o.value("highContrast").toBool(false);
    s.uiScale = std::clamp(o.value("uiScale").toDouble(1.0), 0.75, 2.0);
    s.keepAutosavesOnClose = o.value("keepAutosavesOnClose").toBool(true);
    s.cacheRoot = o.value("cacheRoot").toString();
    return s;
}

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

bool Settings::load()
{
    QJsonObject o;
    if (!readJson(AppPaths::preferencesDir() + "/settings.json", o))
        return false;
    *this = fromJson(o);
    return true;
}

bool Settings::save() const
{
    return writeJsonAtomically(AppPaths::preferencesDir() + "/settings.json", toJson());
}

// ---------------------------------------------------------------- Shortcuts

Shortcuts &Shortcuts::instance()
{
    static Shortcuts s;
    return s;
}

void Shortcuts::registerAction(const QString &id, const QString &label, const QKeySequence &def)
{
    Entry e{id, label, def, def};
    if (m_loaded.contains(id))
        e.key = QKeySequence::fromString(m_loaded.value(id), QKeySequence::PortableText);
    if (m_index.contains(id)) {
        m_entries[m_index[id]] = e;
        return;
    }
    m_index[id] = m_entries.size();
    m_entries << e;
}

QKeySequence Shortcuts::key(const QString &id) const
{
    return m_index.contains(id) ? m_entries[m_index[id]].key : QKeySequence();
}

void Shortcuts::setKey(const QString &id, const QKeySequence &k)
{
    if (m_index.contains(id))
        m_entries[m_index[id]].key = k;
}

void Shortcuts::resetAll()
{
    for (auto &e : m_entries)
        e.key = e.defaultKey;
}

QVector<Shortcuts::Entry> Shortcuts::entries() const
{
    return m_entries;
}

QStringList Shortcuts::conflicts() const
{
    QStringList out;
    QHash<QString, QString> seen;
    for (const auto &e : m_entries) {
        if (e.key.isEmpty())
            continue;
        const QString k = e.key.toString(QKeySequence::PortableText);
        if (seen.contains(k))
            out << QString("%1 is used by both \"%2\" and \"%3\"").arg(e.key.toString(QKeySequence::NativeText), seen[k], e.label);
        else
            seen[k] = e.label;
    }
    return out;
}

bool Shortcuts::load()
{
    QJsonObject o;
    if (!readJson(AppPaths::preferencesDir() + "/shortcuts.json", o))
        return false;
    m_loaded.clear();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_loaded[it.key()] = it.value().toString();
    for (auto &e : m_entries)
        if (m_loaded.contains(e.id))
            e.key = QKeySequence::fromString(m_loaded.value(e.id), QKeySequence::PortableText);
    return true;
}

bool Shortcuts::save() const
{
    QJsonObject o;
    for (const auto &e : m_entries)
        o[e.id] = e.key.toString(QKeySequence::PortableText);
    return writeJsonAtomically(AppPaths::preferencesDir() + "/shortcuts.json", o);
}

// ---------------------------------------------------------------- Recent

QStringList RecentProjects::list()
{
    QJsonObject o;
    readJson(AppPaths::preferencesDir() + "/recent.json", o);
    QStringList out;
    for (const QJsonValue &v : o.value("projects").toArray())
        out << v.toString();
    return out;
}

static void writeRecent(const QStringList &l)
{
    QJsonObject o;
    o["projects"] = QJsonArray::fromStringList(l);
    writeJsonAtomically(AppPaths::preferencesDir() + "/recent.json", o);
}

void RecentProjects::add(const QString &projectDir)
{
    QStringList l = list();
    l.removeAll(projectDir);
    l.prepend(projectDir);
    while (l.size() > 20)
        l.removeLast();
    writeRecent(l);
}

void RecentProjects::remove(const QString &projectDir)
{
    QStringList l = list();
    l.removeAll(projectDir);
    writeRecent(l);
}

Policy Policy::load()
{
    Policy p;
    QJsonObject o;
    if (!readJson(AppPaths::preferencesDir() + "/policy.json", o))
        return p;
    p.cacheRetentionDays = o.value("cacheRetentionDays").toInt(0);
    p.defaultExport = o.value("defaultExport").toObject();
    p.allowUserModels = o.value("allowUserModels").toBool(true);
    return p;
}

} // namespace ss
