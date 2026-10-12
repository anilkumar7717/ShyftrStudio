#include "ProjectSerializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <limits>

namespace ss {

namespace {

qint64 I(const QJsonObject &o, const char *k, qint64 def = 0)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toInteger(def) : def;
}
double D(const QJsonObject &o, const char *k, double def = 0)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble(def) : def;
}
QString S(const QJsonObject &o, const char *k, const QString &def = {})
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isString() ? v.toString() : def;
}
bool B(const QJsonObject &o, const char *k, bool def = false)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isBool() ? v.toBool() : def;
}

} // namespace

QString ProjectSerializer::colorToString(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor ProjectSerializer::colorFromString(const QString &s, const QColor &fallback)
{
    if (s.isEmpty())
        return fallback;
    QColor c = QColor::fromString(s);
    return c.isValid() ? c : fallback;
}

QJsonObject ProjectSerializer::mediaToJson(const MediaAsset &m)
{
    QJsonObject o;
    o["id"] = m.id;
    o["name"] = m.name;
    o["path"] = m.path;
    o["relativePath"] = m.relativePath;
    o["kind"] = toString(m.kind);
    o["duration"] = m.duration;
    o["width"] = m.width;
    o["height"] = m.height;
    o["fps"] = m.fps.toString();
    o["vfr"] = m.variableFrameRate;
    o["hasVideo"] = m.hasVideo;
    o["hasAudio"] = m.hasAudio;
    o["audioChannels"] = m.audioChannels;
    o["sampleRate"] = m.sampleRate;
    o["rotation"] = m.rotation;
    o["fileSize"] = m.fileSize;
    o["videoCodec"] = m.videoCodec;
    o["audioCodec"] = m.audioCodec;
    return o;
}

MediaAsset ProjectSerializer::mediaFromJson(const QJsonObject &o)
{
    MediaAsset m;
    m.id = S(o, "id");
    m.name = S(o, "name");
    m.path = S(o, "path");
    m.relativePath = S(o, "relativePath");
    m.kind = mediaKindFromString(S(o, "kind"));
    m.duration = I(o, "duration");
    m.width = int(I(o, "width"));
    m.height = int(I(o, "height"));
    m.fps = Rational::fromString(S(o, "fps", "30/1"));
    m.variableFrameRate = B(o, "vfr");
    m.hasVideo = B(o, "hasVideo", m.kind != MediaKind::Audio);
    m.hasAudio = B(o, "hasAudio", m.kind != MediaKind::Image);
    m.audioChannels = int(I(o, "audioChannels"));
    m.sampleRate = int(I(o, "sampleRate"));
    m.rotation = int(I(o, "rotation"));
    m.fileSize = I(o, "fileSize");
    m.videoCodec = S(o, "videoCodec");
    m.audioCodec = S(o, "audioCodec");
    return m;
}

QJsonObject ProjectSerializer::clipToJson(const Clip &c)
{
    QJsonObject o;
    o["id"] = c.id;
    o["type"] = toString(c.type);
    if (!c.mediaId.isEmpty())
        o["mediaId"] = c.mediaId;
    o["name"] = c.name;
    o["start"] = c.start;
    o["in"] = c.in;
    o["out"] = c.out;
    o["speed"] = c.speed;
    if (!c.linkId.isEmpty())
        o["linkId"] = c.linkId;
    o["enabled"] = c.enabled;
    o["fadeIn"] = c.fadeIn;
    o["fadeOut"] = c.fadeOut;

    QJsonObject tr;
    tr["cropL"] = c.transform.cropL;
    tr["cropT"] = c.transform.cropT;
    tr["cropR"] = c.transform.cropR;
    tr["cropB"] = c.transform.cropB;
    tr["rotation"] = c.transform.rotation;
    tr["flipH"] = c.transform.flipH;
    tr["flipV"] = c.transform.flipV;
    tr["fit"] = c.transform.fit;
    tr["scale"] = c.transform.scale;
    tr["posX"] = c.transform.posX;
    tr["posY"] = c.transform.posY;
    tr["opacity"] = c.transform.opacity;
    o["transform"] = tr;

    QJsonObject col;
    col["exposure"] = c.color.exposure;
    col["contrast"] = c.color.contrast;
    col["saturation"] = c.color.saturation;
    col["temperature"] = c.color.temperature;
    col["tint"] = c.color.tint;
    o["color"] = col;

    QJsonObject au;
    au["gainDb"] = c.audio.gainDb;
    au["fadeIn"] = c.audio.fadeIn;
    au["fadeOut"] = c.audio.fadeOut;
    au["mute"] = c.audio.mute;
    o["audio"] = au;

    if (c.type == ClipType::Title) {
        const TitleProps &t = c.title;
        QJsonObject to;
        to["text"] = t.text;
        to["fontFamily"] = t.fontFamily;
        to["weight"] = t.weight;
        to["italic"] = t.italic;
        to["sizePct"] = t.sizePct;
        to["color"] = colorToString(t.color);
        to["outlineColor"] = colorToString(t.outlineColor);
        to["outlineWidthPct"] = t.outlineWidthPct;
        to["backgroundColor"] = colorToString(t.backgroundColor);
        to["x"] = t.x;
        to["y"] = t.y;
        to["align"] = t.align;
        to["animation"] = t.animation;
        to["animationDuration"] = t.animationDuration;
        o["title"] = to;
    }
    if (c.type == ClipType::Shape) {
        const ShapeProps &s = c.shape;
        QJsonObject so;
        so["shape"] = s.shape;
        so["fill"] = colorToString(s.fill);
        so["stroke"] = colorToString(s.stroke);
        so["strokeWidthPct"] = s.strokeWidthPct;
        so["x"] = s.x;
        so["y"] = s.y;
        so["w"] = s.w;
        so["h"] = s.h;
        so["radiusPct"] = s.radiusPct;
        o["shape"] = so;
    }
    return o;
}

Clip ProjectSerializer::clipFromJson(const QJsonObject &o)
{
    Clip c;
    c.id = S(o, "id");
    c.type = clipTypeFromString(S(o, "type"));
    c.mediaId = S(o, "mediaId");
    c.name = S(o, "name");
    c.start = I(o, "start");
    c.in = I(o, "in");
    c.out = I(o, "out");
    c.speed = D(o, "speed", 1.0);
    c.linkId = S(o, "linkId");
    c.enabled = B(o, "enabled", true);
    c.fadeIn = I(o, "fadeIn");
    c.fadeOut = I(o, "fadeOut");

    const QJsonObject tr = o.value("transform").toObject();
    c.transform.cropL = D(tr, "cropL");
    c.transform.cropT = D(tr, "cropT");
    c.transform.cropR = D(tr, "cropR");
    c.transform.cropB = D(tr, "cropB");
    c.transform.rotation = D(tr, "rotation");
    c.transform.flipH = B(tr, "flipH");
    c.transform.flipV = B(tr, "flipV");
    c.transform.fit = S(tr, "fit", "fit");
    c.transform.scale = D(tr, "scale", 1.0);
    c.transform.posX = D(tr, "posX");
    c.transform.posY = D(tr, "posY");
    c.transform.opacity = D(tr, "opacity", 1.0);

    const QJsonObject col = o.value("color").toObject();
    c.color.exposure = D(col, "exposure");
    c.color.contrast = D(col, "contrast", 1.0);
    c.color.saturation = D(col, "saturation", 1.0);
    c.color.temperature = D(col, "temperature");
    c.color.tint = D(col, "tint");

    const QJsonObject au = o.value("audio").toObject();
    c.audio.gainDb = D(au, "gainDb");
    c.audio.fadeIn = I(au, "fadeIn");
    c.audio.fadeOut = I(au, "fadeOut");
    c.audio.mute = B(au, "mute");

    if (o.contains("title")) {
        const QJsonObject to = o.value("title").toObject();
        TitleProps t;
        t.text = S(to, "text", t.text);
        t.fontFamily = S(to, "fontFamily", t.fontFamily);
        t.weight = int(I(to, "weight", t.weight));
        t.italic = B(to, "italic");
        t.sizePct = D(to, "sizePct", t.sizePct);
        t.color = colorFromString(S(to, "color"), t.color);
        t.outlineColor = colorFromString(S(to, "outlineColor"), t.outlineColor);
        t.outlineWidthPct = D(to, "outlineWidthPct");
        t.backgroundColor = colorFromString(S(to, "backgroundColor"), t.backgroundColor);
        t.x = D(to, "x", 0.5);
        t.y = D(to, "y", 0.5);
        t.align = S(to, "align", "center");
        t.animation = S(to, "animation", "fade");
        t.animationDuration = I(to, "animationDuration", t.animationDuration);
        c.title = t;
    }
    if (o.contains("shape")) {
        const QJsonObject so = o.value("shape").toObject();
        ShapeProps s;
        s.shape = S(so, "shape", "rect");
        s.fill = colorFromString(S(so, "fill"), s.fill);
        s.stroke = colorFromString(S(so, "stroke"), s.stroke);
        s.strokeWidthPct = D(so, "strokeWidthPct");
        s.x = D(so, "x", 0.5);
        s.y = D(so, "y", 0.5);
        s.w = D(so, "w", 0.3);
        s.h = D(so, "h", 0.1);
        s.radiusPct = D(so, "radiusPct");
        c.shape = s;
    }
    return c;
}

QJsonObject ProjectSerializer::transcriptToJson(const Transcript &t)
{
    QJsonObject o;
    o["id"] = t.id;
    o["mediaId"] = t.mediaId;
    o["language"] = t.language;
    o["model"] = t.model;
    o["created"] = t.created.toString(Qt::ISODate);
    QJsonArray words;
    for (const Word &w : t.words) {
        QJsonObject wo;
        wo["t"] = w.text;
        wo["s"] = w.start;
        wo["e"] = w.end;
        if (w.confidence >= 0)
            wo["c"] = double(qRound(w.confidence * 1000)) / 1000.0;
        if (w.edited)
            wo["ed"] = true;
        if (w.hardBreakBefore)
            wo["br"] = true;
        words.append(wo);
    }
    o["words"] = words;
    QJsonArray cues;
    for (const Cue &c : t.cues) {
        QJsonObject co;
        co["f"] = c.first;
        co["l"] = c.last;
        if (c.speaker >= 0)
            co["spk"] = c.speaker;
        if (!c.styleOverrides.isEmpty())
            co["style"] = c.styleOverrides;
        if (c.manual)
            co["manual"] = true;
        cues.append(co);
    }
    o["cues"] = cues;
    return o;
}

Transcript ProjectSerializer::transcriptFromJson(const QJsonObject &o)
{
    Transcript t;
    t.id = S(o, "id");
    t.mediaId = S(o, "mediaId");
    t.language = S(o, "language");
    t.model = S(o, "model");
    t.created = QDateTime::fromString(S(o, "created"), Qt::ISODate);
    for (const QJsonValue &v : o.value("words").toArray()) {
        const QJsonObject wo = v.toObject();
        Word w;
        w.text = S(wo, "t");
        w.start = I(wo, "s");
        w.end = I(wo, "e");
        w.confidence = float(D(wo, "c", -1));
        w.edited = B(wo, "ed");
        w.hardBreakBefore = B(wo, "br");
        t.words.append(w);
    }
    for (const QJsonValue &v : o.value("cues").toArray()) {
        const QJsonObject co = v.toObject();
        Cue c;
        c.first = int(I(co, "f"));
        c.last = int(I(co, "l"));
        c.speaker = int(I(co, "spk", -1));
        c.styleOverrides = co.value("style").toObject();
        c.manual = B(co, "manual");
        t.cues.append(c);
    }
    return t;
}

QJsonObject ProjectSerializer::toJson(const Project &p)
{
    QJsonObject root;
    root["app"] = "Shyftr Studio";
    root["schemaVersion"] = kCurrentSchemaVersion;
    root["id"] = p.id;
    root["name"] = p.name;
    root["created"] = p.created.toString(Qt::ISODate);
    root["modified"] = p.modified.toString(Qt::ISODate);

    QJsonObject canvas;
    canvas["width"] = p.canvas.width;
    canvas["height"] = p.canvas.height;
    canvas["fps"] = p.canvas.fps.toString();
    canvas["sampleRate"] = p.canvas.sampleRate;
    canvas["background"] = colorToString(p.canvas.background);
    root["canvas"] = canvas;

    QJsonArray media;
    for (const auto &m : p.media)
        media.append(mediaToJson(m));
    root["media"] = media;

    QJsonArray tracks;
    for (const auto &t : p.tracks) {
        QJsonObject to;
        to["id"] = t.id;
        to["kind"] = toString(t.kind);
        to["name"] = t.name;
        to["muted"] = t.muted;
        to["locked"] = t.locked;
        to["hidden"] = t.hidden;
        to["volumeDb"] = t.volumeDb;
        if (t.kind == TrackKind::Caption) {
            to["transcriptId"] = t.transcriptId;
            to["styleId"] = t.styleId;
            to["styleOverrides"] = t.styleOverrides;
            to["burnIn"] = t.burnIn;
        }
        QJsonArray clips;
        for (const auto &c : t.clips)
            clips.append(clipToJson(c));
        to["clips"] = clips;
        tracks.append(to);
    }
    root["tracks"] = tracks;

    QJsonArray transcripts;
    for (const auto &t : p.transcripts)
        transcripts.append(transcriptToJson(t));
    root["transcripts"] = transcripts;

    QJsonArray speakers;
    for (const auto &s : p.speakers) {
        QJsonObject so;
        so["id"] = s.id;
        so["name"] = s.name;
        so["color"] = colorToString(s.color);
        speakers.append(so);
    }
    root["speakers"] = speakers;

    root["playhead"] = p.playhead;
    root["inPoint"] = p.inPoint;
    root["outPoint"] = p.outPoint;
    root["uiMode"] = p.uiMode;
    root["exportSettings"] = p.exportSettings;
    return root;
}

QByteArray ProjectSerializer::toBytes(const Project &p, bool compact)
{
    return QJsonDocument(toJson(p)).toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
}

bool ProjectSerializer::migrate(QJsonObject &root, QString *error, int *fromVersion)
{
    int v = int(root.value("schemaVersion").toInteger(1));
    if (fromVersion)
        *fromVersion = v;
    if (v > kCurrentSchemaVersion) {
        if (error)
            *error = QString("This project was created by a newer version of Shyftr Studio (schema %1). "
                             "Please update the application; the file was not modified.")
                         .arg(v);
        return false;
    }
    if (v < 1) {
        if (error)
            *error = QString("Unknown project schema version %1.").arg(v);
        return false;
    }
    if (v == 1) {
        // v1 -> v2: milliseconds to microseconds, root-level "captions" to transcripts + caption track.
        auto msToUs = [](QJsonObject &o, const char *from, const char *to) {
            if (o.contains(from)) {
                o[to] = o.value(from).toInteger() * kUsPerMs;
                o.remove(from);
            }
        };
        QJsonArray tracks = root.value("tracks").toArray();
        for (int i = 0; i < tracks.size(); ++i) {
            QJsonObject t = tracks[i].toObject();
            QJsonArray clips = t.value("clips").toArray();
            for (int j = 0; j < clips.size(); ++j) {
                QJsonObject c = clips[j].toObject();
                msToUs(c, "startMs", "start");
                msToUs(c, "inMs", "in");
                msToUs(c, "outMs", "out");
                msToUs(c, "fadeInMs", "fadeIn");
                msToUs(c, "fadeOutMs", "fadeOut");
                clips[j] = c;
            }
            t["clips"] = clips;
            tracks[i] = t;
        }
        QJsonArray media = root.value("media").toArray();
        for (int i = 0; i < media.size(); ++i) {
            QJsonObject m = media[i].toObject();
            msToUs(m, "durationMs", "duration");
            media[i] = m;
        }
        root["media"] = media;

        QJsonArray transcripts;
        if (root.contains("captions")) {
            const QJsonObject cap = root.value("captions").toObject();
            QJsonObject tr;
            tr["id"] = newId();
            tr["mediaId"] = cap.value("mediaId").toString();
            tr["language"] = cap.value("language").toString();
            QJsonArray words;
            for (const QJsonValue &wv : cap.value("words").toArray()) {
                QJsonObject w = wv.toObject();
                QJsonObject nw;
                nw["t"] = w.value("t").toString();
                nw["s"] = w.value("s").toInteger() * kUsPerMs;
                nw["e"] = w.value("e").toInteger() * kUsPerMs;
                if (w.contains("c"))
                    nw["c"] = w.value("c");
                words.append(nw);
            }
            tr["words"] = words;
            tr["cues"] = QJsonArray(); // re-segmented on load by the caption engine
            transcripts.append(tr);

            QJsonObject ct;
            ct["id"] = newId();
            ct["kind"] = "caption";
            ct["name"] = "Captions";
            ct["transcriptId"] = tr["id"];
            ct["styleId"] = cap.value("template").toString("bold-highlight-yellow");
            ct["burnIn"] = true;
            ct["clips"] = QJsonArray();
            tracks.append(ct);
            root.remove("captions");
        }
        root["tracks"] = tracks;
        root["transcripts"] = transcripts;
        if (root.contains("playheadMs")) {
            root["playhead"] = root.value("playheadMs").toInteger() * kUsPerMs;
            root.remove("playheadMs");
        }
        root["schemaVersion"] = 2;
        v = 2;
    }
    return v == kCurrentSchemaVersion;
}

bool ProjectSerializer::fromJson(const QJsonObject &rootIn, Project &p, QString *error, int *migratedFrom)
{
    QJsonObject root = rootIn;
    int from = 0;
    if (!migrate(root, error, &from))
        return false;
    if (migratedFrom)
        *migratedFrom = from == kCurrentSchemaVersion ? 0 : from;

    p = Project();
    p.schemaVersion = kCurrentSchemaVersion;
    p.id = S(root, "id", newId());
    p.name = S(root, "name", "Untitled");
    p.created = QDateTime::fromString(S(root, "created"), Qt::ISODate);
    p.modified = QDateTime::fromString(S(root, "modified"), Qt::ISODate);

    const QJsonObject canvas = root.value("canvas").toObject();
    p.canvas.width = int(I(canvas, "width", 1920));
    p.canvas.height = int(I(canvas, "height", 1080));
    p.canvas.fps = Rational::fromString(S(canvas, "fps", "30/1"));
    p.canvas.sampleRate = int(I(canvas, "sampleRate", 48000));
    p.canvas.background = colorFromString(S(canvas, "background"), Qt::black);

    for (const QJsonValue &v : root.value("media").toArray())
        p.media.append(mediaFromJson(v.toObject()));

    for (const QJsonValue &v : root.value("tracks").toArray()) {
        const QJsonObject to = v.toObject();
        Track t;
        t.id = S(to, "id", newId());
        t.kind = trackKindFromString(S(to, "kind"));
        t.name = S(to, "name");
        t.muted = B(to, "muted");
        t.locked = B(to, "locked");
        t.hidden = B(to, "hidden");
        t.volumeDb = D(to, "volumeDb");
        t.transcriptId = S(to, "transcriptId");
        t.styleId = S(to, "styleId", t.styleId);
        t.styleOverrides = to.value("styleOverrides").toObject();
        t.burnIn = B(to, "burnIn", true);
        for (const QJsonValue &cv : to.value("clips").toArray())
            t.clips.append(clipFromJson(cv.toObject()));
        t.sortClips();
        p.tracks.append(t);
    }

    for (const QJsonValue &v : root.value("transcripts").toArray())
        p.transcripts.append(transcriptFromJson(v.toObject()));

    for (const QJsonValue &v : root.value("speakers").toArray()) {
        const QJsonObject so = v.toObject();
        Speaker s;
        s.id = int(I(so, "id"));
        s.name = S(so, "name");
        s.color = colorFromString(S(so, "color"), QColor("#FFD43B"));
        p.speakers.append(s);
    }

    p.playhead = I(root, "playhead");
    p.inPoint = I(root, "inPoint", -1);
    p.outPoint = I(root, "outPoint", -1);
    p.uiMode = S(root, "uiMode", "quick");
    p.exportSettings = root.value("exportSettings").toObject();

    const QStringList problems = validate(p);
    if (!problems.isEmpty()) {
        if (error)
            *error = "Project failed validation:\n- " + problems.join("\n- ");
        return false;
    }
    return true;
}

bool ProjectSerializer::fromBytes(const QByteArray &data, Project &out, QString *error, int *migratedFrom)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QString("The project file is not valid JSON (%1 at offset %2).").arg(pe.errorString()).arg(pe.offset);
        return false;
    }
    return fromJson(doc.object(), out, error, migratedFrom);
}

QStringList ProjectSerializer::validate(const Project &p)
{
    QStringList problems;
    if (p.canvas.width < 16 || p.canvas.height < 16 || p.canvas.width > 8192 || p.canvas.height > 8192)
        problems << QString("Canvas size %1x%2 is out of range.").arg(p.canvas.width).arg(p.canvas.height);
    if (p.canvas.width % 2 || p.canvas.height % 2)
        problems << "Canvas width and height must be even numbers.";
    if (!p.canvas.fps.isValid())
        problems << "Canvas frame rate is invalid.";

    QSet<QString> ids;
    auto checkId = [&](const QString &id, const QString &what) {
        if (id.isEmpty())
            problems << QString("%1 has an empty id.").arg(what);
        else if (ids.contains(id))
            problems << QString("Duplicate id '%1' (%2).").arg(id, what);
        ids.insert(id);
    };
    QSet<QString> mediaIds;
    for (const auto &m : p.media) {
        checkId(m.id, "media " + m.name);
        mediaIds.insert(m.id);
    }
    QSet<QString> transcriptIds;
    for (const auto &t : p.transcripts) {
        checkId(t.id, "transcript");
        transcriptIds.insert(t.id);
        if (!t.mediaId.isEmpty() && !mediaIds.contains(t.mediaId))
            problems << QString("Transcript %1 references missing media %2.").arg(t.id, t.mediaId);
        for (int i = 0; i < t.words.size(); ++i)
            if (t.words[i].end < t.words[i].start)
                problems << QString("Transcript %1 word %2 ends before it starts.").arg(t.id).arg(i);
        int prevLast = -1;
        for (int i = 0; i < t.cues.size(); ++i) {
            const Cue &c = t.cues[i];
            if (c.first < 0 || c.last >= t.words.size() || c.first > c.last || c.first <= prevLast) {
                problems << QString("Transcript %1 caption %2 has an invalid word range.").arg(t.id).arg(i);
                break;
            }
            prevLast = c.last;
        }
    }
    for (const auto &t : p.tracks) {
        checkId(t.id, "track " + t.name);
        if (t.kind == TrackKind::Caption && !t.transcriptId.isEmpty() && !transcriptIds.contains(t.transcriptId))
            problems << QString("Caption track %1 references missing transcript.").arg(t.name);
        TimeUs prevEnd = std::numeric_limits<TimeUs>::min();
        for (const auto &c : t.clips) {
            checkId(c.id, "clip " + c.name);
            if (c.speed <= 0 || c.speed > 100)
                problems << QString("Clip %1 has invalid speed %2.").arg(c.name).arg(c.speed);
            if (c.out <= c.in)
                problems << QString("Clip %1 has an empty or negative source range.").arg(c.name);
            if (c.start < 0)
                problems << QString("Clip %1 starts before 0.").arg(c.name);
            if (c.type == ClipType::Media && !mediaIds.contains(c.mediaId))
                problems << QString("Clip %1 references missing media.").arg(c.name);
            if (c.start < prevEnd)
                problems << QString("Clips overlap on track %1 at %2.").arg(t.name, formatClock(c.start));
            prevEnd = c.end();
        }
    }
    return problems;
}

} // namespace ss
