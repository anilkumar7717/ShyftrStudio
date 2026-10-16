#pragma once

#include "Model.h"

#include <QJsonObject>
#include <QStringList>

namespace ss {

// Versioned JSON schema for project.json.
// History:
//   1 - prototype format: times stored in milliseconds ("startMs", "inMs", "outMs", word "s"/"e").
//   2 - microsecond time base, caption tracks bound to transcripts, linked clips, speakers.
constexpr int kCurrentSchemaVersion = 2;

class ProjectSerializer {
public:
    static QJsonObject toJson(const Project &p);
    static QByteArray toBytes(const Project &p, bool compact = false);

    // Parses (and migrates, if needed) a project document.
    // migratedFrom receives the original schema version when a migration ran (0 otherwise).
    static bool fromJson(const QJsonObject &root, Project &out, QString *error, int *migratedFrom = nullptr);
    static bool fromBytes(const QByteArray &data, Project &out, QString *error, int *migratedFrom = nullptr);

    // Explicit, tested migration steps. Returns false on unsupported versions.
    static bool migrate(QJsonObject &root, QString *error, int *fromVersion);

    // Structural validation. Returns a list of human readable problems (empty = valid).
    static QStringList validate(const Project &p);
    // True when a and b would save identically (ignoring playhead and the modified time).
    // Fast for the usual edit: parts the edit did not touch are still shared (Qt implicit
    // sharing) and are not compared at all.
    static bool sameContent(const Project &a, const Project &b);

    // Partial (de)serialisers reused by templates/clipboard/tests.
    static QJsonObject clipToJson(const Clip &c);
    static Clip clipFromJson(const QJsonObject &o);
    static QJsonObject transcriptToJson(const Transcript &t);
    static Transcript transcriptFromJson(const QJsonObject &o);
    static QJsonObject mediaToJson(const MediaAsset &m);
    static MediaAsset mediaFromJson(const QJsonObject &o);

    static QString colorToString(const QColor &c);
    static QColor colorFromString(const QString &s, const QColor &fallback = Qt::white);
};

} // namespace ss
