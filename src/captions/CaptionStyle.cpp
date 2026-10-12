#include "CaptionStyle.h"
#include "../core/Settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

// Resources live in a static library; make sure they are registered.
static void initShyftrResources() { Q_INIT_RESOURCE(resources); }

namespace ss {

static QString c2s(const QColor &c) { return c.name(QColor::HexArgb); }
static QColor s2c(const QJsonValue &v, const QColor &def)
{
    if (!v.isString())
        return def;
    QColor c = QColor::fromString(v.toString());
    return c.isValid() ? c : def;
}

QJsonObject CaptionStyle::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["family"] = family;
    o["description"] = description;
    o["rank"] = rank;
    o["fontFamilies"] = QJsonArray::fromStringList(fontFamilies);
    o["fontWeight"] = fontWeight;
    o["italic"] = italic;
    o["sizePct"] = sizePct;
    o["uppercase"] = uppercase;
    o["letterSpacingPct"] = letterSpacingPct;
    o["lineSpacing"] = lineSpacing;
    o["textColor"] = c2s(textColor);
    o["activeColor"] = c2s(activeColor);
    o["outlineColor"] = c2s(outlineColor);
    o["outlineWidthPct"] = outlineWidthPct;
    o["shadowColor"] = c2s(shadowColor);
    o["shadowOffsetPct"] = shadowOffsetPct;
    o["glowColor"] = c2s(glowColor);
    o["glowRadiusPct"] = glowRadiusPct;
    o["gradientColor"] = c2s(gradientColor);
    o["boxMode"] = boxMode;
    o["boxColor"] = c2s(boxColor);
    o["boxPaddingPct"] = boxPaddingPct;
    o["boxRadiusPct"] = boxRadiusPct;
    o["anchor"] = anchor;
    o["offsetYPct"] = offsetYPct;
    o["offsetXPct"] = offsetXPct;
    o["align"] = align;
    o["maxWidthPct"] = maxWidthPct;
    o["maxWordsPerLine"] = maxWordsPerLine;
    o["maxCharsPerLine"] = maxCharsPerLine;
    o["maxLines"] = maxLines;
    o["wordEffect"] = wordEffect;
    o["popScale"] = popScale;
    o["activeRotateDeg"] = activeRotateDeg;
    o["entry"] = entry;
    o["exit"] = exit;
    o["animMs"] = animMs;
    o["showSpeakerLabel"] = showSpeakerLabel;
    o["speakerColors"] = speakerColors;
    o["speakerPlacement"] = speakerPlacement;
    o["pauseBreakMs"] = pauseBreakMs;
    o["maxCueMs"] = maxCueMs;
    o["minCueMs"] = minCueMs;
    return o;
}

CaptionStyle CaptionStyle::fromJson(const QJsonObject &o)
{
    return fromJson(o, CaptionStyle());
}

CaptionStyle CaptionStyle::fromJson(const QJsonObject &o, const CaptionStyle &base)
{
    CaptionStyle s = base;
    auto str = [&](const char *k, QString &dst) { if (o.contains(k)) dst = o.value(k).toString(dst); };
    auto num = [&](const char *k, double &dst) { if (o.contains(k)) dst = o.value(k).toDouble(dst); };
    auto inum = [&](const char *k, int &dst) { if (o.contains(k)) dst = o.value(k).toInt(dst); };
    auto bln = [&](const char *k, bool &dst) { if (o.contains(k)) dst = o.value(k).toBool(dst); };
    auto col = [&](const char *k, QColor &dst) { if (o.contains(k)) dst = s2c(o.value(k), dst); };

    str("id", s.id);
    str("name", s.name);
    str("family", s.family);
    str("description", s.description);
    inum("rank", s.rank);
    if (o.contains("fontFamilies")) {
        QStringList f;
        for (const QJsonValue &v : o.value("fontFamilies").toArray())
            f << v.toString();
        if (!f.isEmpty())
            s.fontFamilies = f;
    }
    inum("fontWeight", s.fontWeight);
    bln("italic", s.italic);
    num("sizePct", s.sizePct);
    bln("uppercase", s.uppercase);
    num("letterSpacingPct", s.letterSpacingPct);
    num("lineSpacing", s.lineSpacing);
    col("textColor", s.textColor);
    col("activeColor", s.activeColor);
    col("outlineColor", s.outlineColor);
    num("outlineWidthPct", s.outlineWidthPct);
    col("shadowColor", s.shadowColor);
    num("shadowOffsetPct", s.shadowOffsetPct);
    col("glowColor", s.glowColor);
    num("glowRadiusPct", s.glowRadiusPct);
    col("gradientColor", s.gradientColor);
    str("boxMode", s.boxMode);
    col("boxColor", s.boxColor);
    num("boxPaddingPct", s.boxPaddingPct);
    num("boxRadiusPct", s.boxRadiusPct);
    str("anchor", s.anchor);
    num("offsetYPct", s.offsetYPct);
    num("offsetXPct", s.offsetXPct);
    str("align", s.align);
    num("maxWidthPct", s.maxWidthPct);
    inum("maxWordsPerLine", s.maxWordsPerLine);
    inum("maxCharsPerLine", s.maxCharsPerLine);
    inum("maxLines", s.maxLines);
    str("wordEffect", s.wordEffect);
    num("popScale", s.popScale);
    num("activeRotateDeg", s.activeRotateDeg);
    str("entry", s.entry);
    str("exit", s.exit);
    inum("animMs", s.animMs);
    bln("showSpeakerLabel", s.showSpeakerLabel);
    bln("speakerColors", s.speakerColors);
    str("speakerPlacement", s.speakerPlacement);
    inum("pauseBreakMs", s.pauseBreakMs);
    inum("maxCueMs", s.maxCueMs);
    inum("minCueMs", s.minCueMs);

    // sanity clamps
    s.sizePct = std::clamp(s.sizePct, 0.01, 0.4);
    s.maxWordsPerLine = std::clamp(s.maxWordsPerLine, 1, 30);
    s.maxCharsPerLine = std::clamp(s.maxCharsPerLine, 4, 120);
    s.maxLines = std::clamp(s.maxLines, 1, 4);
    s.animMs = std::clamp(s.animMs, 0, 2000);
    s.maxWidthPct = std::clamp(s.maxWidthPct, 0.2, 1.0);
    s.offsetXPct = std::clamp(s.offsetXPct, -0.5, 0.5);
    s.offsetYPct = std::clamp(s.offsetYPct, -0.9, 0.9);
    s.activeRotateDeg = std::clamp(s.activeRotateDeg, -30.0, 30.0);
    return s;
}

CaptionStyle CaptionStyle::withOverrides(const QJsonObject &overrides) const
{
    if (overrides.isEmpty())
        return *this;
    QJsonObject o = overrides;
    o.remove("id");
    return fromJson(o, *this);
}

QRectF SafeArea::forFrame(int width, int height)
{
    if (width <= 0 || height <= 0)
        return {};
    const double r = double(width) / height;
    double side, top, bottom;
    if (r < 0.7) {          // 9:16 vertical - avoid platform UI (captions, buttons)
        side = 0.08; top = 0.12; bottom = 0.20;
    } else if (r < 1.2) {   // 1:1 / 4:5
        side = 0.07; top = 0.08; bottom = 0.10;
    } else {                // 16:9 and wider
        side = 0.06; top = 0.07; bottom = 0.08;
    }
    return QRectF(width * side, height * top, width * (1 - 2 * side), height * (1 - top - bottom));
}

// ---------------------------------------------------------------- library

TemplateLibrary &TemplateLibrary::instance()
{
    static TemplateLibrary lib;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        lib.reload();
    }
    return lib;
}

static QVector<CaptionStyle> loadDir(const QString &dir)
{
    QVector<CaptionStyle> out;
    QDir d(dir);
    const QStringList files = d.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QFile file(d.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;
        CaptionStyle s = CaptionStyle::fromJson(doc.object());
        if (s.id.isEmpty())
            s.id = QFileInfo(f).completeBaseName();
        if (s.name.isEmpty())
            s.name = s.id;
        out << s;
    }
    return out;
}

void TemplateLibrary::reload(const QString &projectTemplatesDir)
{
    static bool resInit = false;
    if (!resInit) {
        initShyftrResources();
        resInit = true;
    }
    m_builtIn = loadDir(":/templates");
    std::sort(m_builtIn.begin(), m_builtIn.end(), [](const CaptionStyle &a, const CaptionStyle &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.id < b.id;
    });
    if (m_builtIn.isEmpty()) {
        CaptionStyle s;
        s.id = "bold-highlight-yellow";
        s.name = "Bold Highlight";
        s.family = "Bold highlight";
        m_builtIn << s;
    }
    m_user = loadDir(AppPaths::userTemplatesDir());
    m_project = projectTemplatesDir.isEmpty() ? QVector<CaptionStyle>() : loadDir(projectTemplatesDir);
}

QVector<CaptionStyle> TemplateLibrary::all() const
{
    return m_builtIn + m_user + m_project;
}

bool TemplateLibrary::contains(const QString &id) const
{
    for (const auto &s : all())
        if (s.id == id)
            return true;
    return false;
}

bool TemplateLibrary::isBuiltIn(const QString &id) const
{
    for (const auto &s : m_builtIn)
        if (s.id == id)
            return true;
    return false;
}

CaptionStyle TemplateLibrary::get(const QString &id) const
{
    // project templates override user templates override built-ins
    for (const auto &s : m_project)
        if (s.id == id)
            return s;
    for (const auto &s : m_user)
        if (s.id == id)
            return s;
    for (const auto &s : m_builtIn)
        if (s.id == id)
            return s;
    return m_builtIn.isEmpty() ? CaptionStyle() : m_builtIn.first();
}

static QString slug(const QString &s)
{
    QString out = s.toLower();
    out.replace(QRegularExpression("[^a-z0-9]+"), "-");
    out.remove(QRegularExpression("^-+|-+$"));
    return out.isEmpty() ? "template" : out;
}

bool TemplateLibrary::saveUserTemplate(const CaptionStyle &sIn, QString *error)
{
    CaptionStyle s = sIn;
    if (s.id.isEmpty() || isBuiltIn(s.id))
        s.id = "user-" + slug(s.name);
    QDir().mkpath(AppPaths::userTemplatesDir());
    if (!writeJsonAtomically(AppPaths::userTemplatesDir() + "/" + s.id + ".json", s.toJson(), error))
        return false;
    for (auto &u : m_user)
        if (u.id == s.id) {
            u = s;
            return true;
        }
    m_user << s;
    return true;
}

bool TemplateLibrary::removeUserTemplate(const QString &id)
{
    for (int i = 0; i < m_user.size(); ++i)
        if (m_user[i].id == id) {
            QFile::remove(AppPaths::userTemplatesDir() + "/" + id + ".json");
            m_user.removeAt(i);
            return true;
        }
    return false;
}

bool TemplateLibrary::saveProjectTemplate(const QString &dir, const CaptionStyle &sIn, QString *error)
{
    CaptionStyle s = sIn;
    if (s.id.isEmpty() || isBuiltIn(s.id))
        s.id = "project-" + slug(s.name);
    if (!writeJsonAtomically(dir + "/" + s.id + ".json", s.toJson(), error))
        return false;
    for (auto &u : m_project)
        if (u.id == s.id) {
            u = s;
            return true;
        }
    m_project << s;
    return true;
}

} // namespace ss
