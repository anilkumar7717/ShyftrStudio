#pragma once

#include "../core/Model.h"

namespace ss {

namespace MediaProbe {
// Uses ffprobe to read duration, streams, frame rate (incl. VFR detection) and display
// rotation. Fills a MediaAsset (id/name are set by the caller if empty).
bool probe(const QString &path, MediaAsset &out, QString *error);
bool parseProbeJson(const QByteArray &json, MediaAsset &out, QString *error);
bool isImageFile(const QString &path);
QStringList importFilters(); // for file dialogs
} // namespace MediaProbe

} // namespace ss
