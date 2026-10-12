#pragma once

#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ss {

// A caption template. Stored as JSON (built-in resources, User_Templates/, project templates/).
// The same struct drives preview, burned-in export and ASS interchange, so what you see
// in the viewer is what gets rendered.
struct CaptionStyle {
    QString id;
    QString name;
    QString family;      // template family shown in the gallery
    QString description;
    int rank = 100;      // gallery order (lower first); ties sort by id

    // Typography
    QStringList fontFamilies{"Helvetica Neue", "Arial", "Segoe UI"};
    int fontWeight = 800;
    bool italic = false;
    double sizePct = 0.06;        // of frame height
    bool uppercase = false;
    double letterSpacingPct = 0;  // percent of font size (QFont::PercentageSpacing delta)
    double lineSpacing = 1.1;

    // Colours
    QColor textColor = Qt::white;
    QColor activeColor = QColor("#FFD43B");
    QColor outlineColor = Qt::black;
    double outlineWidthPct = 0.12; // of font pixel size
    QColor shadowColor = QColor(0, 0, 0, 160);
    double shadowOffsetPct = 0.06;
    QColor glowColor = QColor(0, 0, 0, 0);
    double glowRadiusPct = 0.0;
    QColor gradientColor = QColor(0, 0, 0, 0); // bottom colour of a vertical text gradient (transparent = solid text)

    // Background box: none | line | cue | activeWord | bar
    QString boxMode = "none";
    QColor boxColor = QColor(0, 0, 0, 170);
    double boxPaddingPct = 0.25;   // of font size
    double boxRadiusPct = 0.25;

    // Layout
    QString anchor = "bottom";     // bottom | middle | top
    double offsetYPct = 0.0;       // extra vertical offset (fraction of frame height, + = up)
    double offsetXPct = 0.0;       // extra horizontal offset (fraction of frame width, + = right)
    QString align = "center";      // left | center | right
    double maxWidthPct = 0.86;     // of safe-area width
    int maxWordsPerLine = 5;
    int maxCharsPerLine = 32;
    int maxLines = 2;

    // Animation
    // wordEffect: none | highlight | karaoke | pop | reveal | typewriter | activeOnly
    QString wordEffect = "highlight";
    double popScale = 1.18;
    double activeRotateDeg = 0;    // tilt of the spoken word (degrees), for punchy creator styles
    QString entry = "fade";        // none | fade | pop | slideUp
    QString exit = "fade";         // none | fade
    int animMs = 120;

    // Speakers
    bool showSpeakerLabel = false;
    bool speakerColors = false;    // colour text by speaker
    QString speakerPlacement = "none"; // none | alternateSides

    // Segmentation hints
    int pauseBreakMs = 450;
    int maxCueMs = 5000;
    int minCueMs = 700;

    QJsonObject toJson() const;
    static CaptionStyle fromJson(const QJsonObject &o);
    static CaptionStyle fromJson(const QJsonObject &o, const CaptionStyle &base);
    // Applies a partial override object on top of a style.
    CaptionStyle withOverrides(const QJsonObject &overrides) const;
    int maxWordsPerCue() const { return std::max(1, maxWordsPerLine * maxLines); }
};

// Caption-safe regions per aspect ratio (16:9, 9:16, 1:1 and others).
struct SafeArea {
    static QRectF forFrame(int width, int height);
};

class TemplateLibrary {
public:
    static TemplateLibrary &instance();
    void reload(const QString &projectTemplatesDir = {});
    QVector<CaptionStyle> all() const;
    QVector<CaptionStyle> builtIns() const { return m_builtIn; }
    CaptionStyle get(const QString &id) const; // falls back to the first built-in
    bool contains(const QString &id) const;
    bool saveUserTemplate(const CaptionStyle &s, QString *error = nullptr);
    bool removeUserTemplate(const QString &id);
    bool saveProjectTemplate(const QString &projectTemplatesDir, const CaptionStyle &s, QString *error = nullptr);
    bool isBuiltIn(const QString &id) const;

private:
    QVector<CaptionStyle> m_builtIn;
    QVector<CaptionStyle> m_user;
    QVector<CaptionStyle> m_project;
};

} // namespace ss
