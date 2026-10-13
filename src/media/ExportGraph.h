#pragma once

#include "../core/Model.h"

#include <QJsonObject>
#include <QRectF>
#include <QStringList>

namespace ss {

struct ExportSettings {
    QString presetId = "youtube-1080p";
    int width = 1920;
    int height = 1080;
    Rational fps{30, 1};
    QString videoCodec = "h264";      // h264 | hevc
    QString encoder = "auto";         // auto | software | hardware
    QString rateMode = "crf";         // crf | bitrate
    int crf = 20;
    int videoBitrateKbps = 12000;
    int audioBitrateKbps = 192;
    QString speedPreset = "medium";   // x264/x265 preset
    TimeUs rangeStart = 0;
    TimeUs rangeEnd = -1;             // -1 = project end
    bool burnCaptions = true;
    bool writeSrt = false;
    bool writeVtt = false;
    bool normalizeLoudness = false;   // EBU R128 to -14 LUFS (streaming platforms)
    QString outputPath;

    QJsonObject toJson() const;
    static ExportSettings fromJson(const QJsonObject &o);
};

struct ExportPreset {
    QString id;
    QString label;
    int width, height;
    int fpsNum, fpsDen;
    QString codec;
    int crf;
    int bitrateKbps;
};
QVector<ExportPreset> exportPresets();

// Shared geometry for preview and export (crop -> flip -> rotate -> fit/scale -> position).
struct ClipGeometry {
    QRectF crop;        // in source pixels
    int quarterTurns = 0; // clockwise 90° steps
    double freeAngle = 0; // remaining degrees (clockwise)
    double boxW = 0, boxH = 0; // size of the rotated, cropped image before scaling
    double scale = 1;   // uniform scale (fit/fill * user scale)
    double scaleX = 1, scaleY = 1; // stretch mode
    double outW = 0, outH = 0;     // final size on the canvas
    double x = 0, y = 0;           // top-left on the canvas
};
ClipGeometry computeGeometry(int srcW, int srcH, const Transform &t, int canvasW, int canvasH);

struct ExportPlan {
    QStringList args;          // ffmpeg arguments (without the program)
    QString filterGraph;       // written to a script file by the job
    QString filterScriptPath;  // placeholder replaced in args
    bool usesOverlayPipe = false;
    bool compositorVideo = false; // pipe frames are the complete picture
    bool audioOnly = false;
    int overlayInputIndex = -1;
    TimeUs duration = 0;
    qint64 frameCount = 0;
    QString encoderName;
    QStringList warnings;
    QString error;
};

class ExportGraph {
public:
    // Builds a deterministic FFmpeg filter graph for the timeline:
    //  * each video track becomes one exact-length RGBA stream (clips + transparent gaps)
    //  * tracks are composited bottom->top over the canvas background
    //  * titles/shapes/captions arrive as a raw RGBA pipe rendered by OverlayScene
    //  * audio clips are trimmed, retimed, gain/faded, delayed and mixed
    // compositorVideo: the whole picture arrives on pipe:0 as raw RGBA (Qt compositor path).
    // An audio extension (.mp3 .wav .m4a .aac .flac) produces an audio-only file.
    static ExportPlan build(const Project &p, const ExportSettings &s, bool needsOverlay, int ffmpegMajor,
                            const QStringList &availableEncoders, const QString &scriptPath, bool compositorVideo = false);
    static bool isAudioOnlyPath(const QString &path);
    static QString chooseEncoder(const QString &codec, const QString &pref, const QStringList &available,
                                 QStringList *warnings = nullptr);
    static int estimateBitrateKbps(int w, int h, Rational fps, int crf);
    static QStringList atempoChain(double speed);
};

} // namespace ss
