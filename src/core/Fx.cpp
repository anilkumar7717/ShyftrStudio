#include "Fx.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace ss {

// ------------------------------------------------------------------ keyframes

double Keyframes::ease(const QString &c, double u)
{
    u = std::clamp(u, 0.0, 1.0);
    if (c == "linear")
        return u;
    if (c == "hold")
        return 0.0;
    if (c == "easeIn")
        return u * u * u;
    if (c == "easeOut")
        return 1 - std::pow(1 - u, 3);
    if (c == "bounce") {
        // overshoot then settle (easeOutBack)
        const double c1 = 1.70158, c3 = c1 + 1;
        return 1 + c3 * std::pow(u - 1, 3) + c1 * std::pow(u - 1, 2);
    }
    return u * u * (3 - 2 * u); // smooth
}

QStringList Keyframes::easeNames()
{
    return {"smooth", "linear", "easeIn", "easeOut", "bounce", "hold"};
}

void Keyframes::sort(QVector<Keyframe> &keys)
{
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe &a, const Keyframe &b) { return a.t < b.t; });
}

int Keyframes::indexAt(const QVector<Keyframe> &keys, TimeUs t, TimeUs tol)
{
    for (int i = 0; i < keys.size(); ++i)
        if (std::llabs(keys[i].t - t) <= tol)
            return i;
    return -1;
}

AnimatedValues Keyframes::evaluate(const QVector<Keyframe> &keys, TimeUs t)
{
    AnimatedValues v;
    if (keys.isEmpty())
        return v;
    auto from = [](const Keyframe &k) {
        AnimatedValues a;
        a.x = k.x;
        a.y = k.y;
        a.scale = k.scale;
        a.rotation = k.rotation;
        a.opacity = k.opacity;
        return a;
    };
    if (t <= keys.first().t)
        return from(keys.first());
    if (t >= keys.last().t)
        return from(keys.last());
    for (int i = 0; i + 1 < keys.size(); ++i) {
        const Keyframe &a = keys[i], &b = keys[i + 1];
        if (t >= a.t && t <= b.t) {
            const double span = std::max<TimeUs>(1, b.t - a.t);
            const double e = ease(a.ease, double(t - a.t) / span); // the curve leaving key a
            auto mix = [e](double p, double q) { return p + (q - p) * e; };
            v.x = mix(a.x, b.x);
            v.y = mix(a.y, b.y);
            v.scale = mix(a.scale, b.scale);
            v.rotation = mix(a.rotation, b.rotation);
            v.opacity = std::clamp(mix(a.opacity, b.opacity), 0.0, 1.0);
            return v;
        }
    }
    return from(keys.last());
}

// ------------------------------------------------------------------ adjust

bool ExtraAdjust::isIdentity() const
{
    return hue == 0 && vibrance == 0 && highlights == 0 && shadows == 0 && fade == 0 && vignette == 0 && grain == 0
        && sharpen == 0 && lut.isEmpty();
}

// ------------------------------------------------------------------ speed curves

static double segIntegral(const QPointF &a, const QPointF &b, double u)
{
    // integral of the linear speed between a and b from a.x() to u
    const double w = std::clamp(u, a.x(), b.x()) - a.x();
    if (w <= 0)
        return 0;
    const double slope = (b.x() > a.x()) ? (b.y() - a.y()) / (b.x() - a.x()) : 0;
    return a.y() * w + 0.5 * slope * w * w;
}

double SpeedCurve::sourceFraction(double u) const
{
    if (!isSet())
        return std::clamp(u, 0.0, 1.0);
    u = std::clamp(u, 0.0, 1.0);
    double total = 0, part = 0;
    for (int i = 0; i + 1 < points.size(); ++i) {
        total += segIntegral(points[i], points[i + 1], points[i + 1].x());
        part += segIntegral(points[i], points[i + 1], u);
    }
    return total > 0 ? part / total : u;
}

double SpeedCurve::timelineFraction(double f) const
{
    if (!isSet())
        return std::clamp(f, 0.0, 1.0);
    f = std::clamp(f, 0.0, 1.0);
    double lo = 0, hi = 1;
    for (int i = 0; i < 48; ++i) {
        const double mid = (lo + hi) / 2;
        if (sourceFraction(mid) < f)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) / 2;
}

QStringList SpeedCurve::presetNames()
{
    return {"montage", "hero", "bullet", "jumpCut", "flashIn", "flashOut", "rampUp", "rampDown"};
}

SpeedCurve SpeedCurve::fromPreset(const QString &name)
{
    SpeedCurve c;
    c.preset = name;
    if (name == "montage")
        c.points = {{0, 1}, {0.2, 3}, {0.45, 0.5}, {0.55, 0.5}, {0.8, 3}, {1, 1}};
    else if (name == "hero")
        c.points = {{0, 1}, {0.35, 1}, {0.45, 0.2}, {0.55, 0.2}, {0.65, 1}, {1, 1}};
    else if (name == "bullet")
        c.points = {{0, 3}, {0.4, 3}, {0.5, 0.25}, {0.6, 0.25}, {0.7, 3}, {1, 3}};
    else if (name == "jumpCut")
        c.points = {{0, 0.6}, {0.45, 0.6}, {0.5, 5}, {0.55, 0.6}, {1, 0.6}};
    else if (name == "flashIn")
        c.points = {{0, 5}, {0.3, 1}, {1, 1}};
    else if (name == "flashOut")
        c.points = {{0, 1}, {0.7, 1}, {1, 5}};
    else if (name == "rampUp")
        c.points = {{0, 0.4}, {1, 3}};
    else if (name == "rampDown")
        c.points = {{0, 3}, {1, 0.4}};
    else
        c.preset.clear();
    return c;
}

// ------------------------------------------------------------------ audio

QStringList AudioFx::voiceNames()
{
    return {"chipmunk", "deep", "robot", "echo", "hall", "telephone", "megaphone", "radio", "underwater", "monster"};
}

QString AudioFx::ffmpegChain(int sr) const
{
    QStringList f;
    auto pitch = [&](double k) {
        // pitch shift without changing duration: resample, then restore the tempo
        f << QString("asetrate=%1").arg(int(std::lround(sr * k))) << QString("aresample=%1").arg(sr)
          << QString("atempo=%1").arg(1.0 / k, 0, 'f', 6);
    };
    if (denoise)
        f << "afftdn=nf=-25";
    if (enhance)
        f << "highpass=f=80" << "equalizer=f=3000:t=q:w=1:g=3" << "acompressor=threshold=0.1:ratio=3:attack=10:release=200";
    if (voice == "chipmunk")
        pitch(1.5);
    else if (voice == "deep")
        pitch(0.75);
    else if (voice == "monster") {
        pitch(0.6);
        f << "aecho=0.8:0.6:40:0.3";
    } else if (voice == "robot")
        f << "afftfilt=real='hypot(re,im)*sin(0)':imag='hypot(re,im)*cos(0)':win_size=512:overlap=0.75";
    else if (voice == "echo")
        f << "aecho=0.8:0.88:220:0.4";
    else if (voice == "hall")
        f << "aecho=0.8:0.85:40|70|110|160:0.35|0.3|0.25|0.2";
    else if (voice == "telephone")
        f << "highpass=f=300" << "lowpass=f=3400" << "volume=1.4";
    else if (voice == "megaphone")
        f << "highpass=f=500" << "lowpass=f=4000" << "acrusher=bits=8:mode=log:aa=1" << "volume=1.6";
    else if (voice == "radio")
        f << "highpass=f=200" << "lowpass=f=5000" << "acompressor=threshold=0.08:ratio=4";
    else if (voice == "underwater")
        f << "lowpass=f=500" << "aecho=0.8:0.7:30:0.3";
    return f.join(',');
}

// ------------------------------------------------------------------ ClipFx

bool ClipFx::isDefault() const
{
    return keyframes.isEmpty() && !transitionIn.isSet() && effects.isEmpty() && !chroma.enabled && !mask.isSet()
        && blend == "normal" && !background.isSet() && adjust.isIdentity() && look.isEmpty() && !speedCurve.isSet()
        && audio.isDefault();
}

bool ClipFx::needsCompositor() const
{
    return !keyframes.isEmpty() || transitionIn.isSet() || !effects.isEmpty() || chroma.enabled || mask.isSet()
        || blend != "normal" || background.isSet() || !adjust.isIdentity() || speedCurve.isSet();
}

static QString col(const QColor &c) { return c.name(QColor::HexArgb); }
static QColor col(const QJsonValue &v, const QColor &def)
{
    const QColor c = QColor::fromString(v.toString());
    return c.isValid() ? c : def;
}

QJsonObject ClipFx::toJson() const
{
    QJsonObject o;
    if (!keyframes.isEmpty()) {
        QJsonArray a;
        for (const Keyframe &k : keyframes)
            a.append(QJsonObject{{"t", k.t}, {"x", k.x}, {"y", k.y}, {"scale", k.scale}, {"rotation", k.rotation},
                                 {"opacity", k.opacity}, {"ease", k.ease}});
        o["keyframes"] = a;
    }
    if (transitionIn.isSet())
        o["transitionIn"] = QJsonObject{{"type", transitionIn.type}, {"duration", transitionIn.duration}};
    if (!effects.isEmpty()) {
        QJsonArray a;
        for (const EffectSpec &e : effects)
            a.append(QJsonObject{{"type", e.type}, {"intensity", e.intensity}, {"speed", e.speed}});
        o["effects"] = a;
    }
    if (chroma.enabled)
        o["chroma"] = QJsonObject{{"color", col(chroma.color)}, {"similarity", chroma.similarity},
                                  {"smoothness", chroma.smoothness}, {"spill", chroma.spill}};
    if (mask.isSet())
        o["mask"] = QJsonObject{{"type", mask.type}, {"cx", mask.cx}, {"cy", mask.cy}, {"w", mask.w}, {"h", mask.h},
                                {"angle", mask.angle}, {"feather", mask.feather}, {"radius", mask.radius},
                                {"invert", mask.invert}};
    if (blend != "normal")
        o["blend"] = blend;
    if (background.isSet())
        o["background"] = QJsonObject{{"mode", background.mode}, {"color", col(background.color)}, {"blur", background.blur}};
    if (!adjust.isIdentity()) {
        QJsonObject a{{"hue", adjust.hue}, {"vibrance", adjust.vibrance}, {"highlights", adjust.highlights},
                      {"shadows", adjust.shadows}, {"fade", adjust.fade}, {"vignette", adjust.vignette},
                      {"grain", adjust.grain}, {"sharpen", adjust.sharpen}};
        if (!adjust.lut.isEmpty()) {
            a["lut"] = adjust.lut;
            a["lutStrength"] = adjust.lutStrength;
        }
        o["adjust"] = a;
    }
    if (!look.isEmpty())
        o["look"] = look;
    if (speedCurve.isSet()) {
        QJsonArray pts;
        for (const QPointF &pt : speedCurve.points)
            pts.append(QJsonArray{pt.x(), pt.y()});
        o["speedCurve"] = QJsonObject{{"preset", speedCurve.preset}, {"points", pts}};
    }
    if (!audio.isDefault())
        o["audioFx"] = QJsonObject{{"voice", audio.voice}, {"denoise", audio.denoise}, {"enhance", audio.enhance}};
    return o;
}

ClipFx ClipFx::fromJson(const QJsonObject &o)
{
    ClipFx fx;
    for (const QJsonValue &v : o.value("keyframes").toArray()) {
        const QJsonObject k = v.toObject();
        Keyframe kf;
        kf.t = k.value("t").toInteger();
        kf.x = k.value("x").toDouble();
        kf.y = k.value("y").toDouble();
        kf.scale = k.value("scale").toDouble(1.0);
        kf.rotation = k.value("rotation").toDouble();
        kf.opacity = std::clamp(k.value("opacity").toDouble(1.0), 0.0, 1.0);
        kf.ease = k.value("ease").toString("smooth");
        fx.keyframes << kf;
    }
    Keyframes::sort(fx.keyframes);
    const QJsonObject tr = o.value("transitionIn").toObject();
    fx.transitionIn.type = tr.value("type").toString();
    fx.transitionIn.duration = tr.value("duration").toInteger(500000);
    for (const QJsonValue &v : o.value("effects").toArray()) {
        const QJsonObject e = v.toObject();
        EffectSpec es;
        es.type = e.value("type").toString();
        es.intensity = std::clamp(e.value("intensity").toDouble(0.6), 0.0, 1.0);
        es.speed = std::clamp(e.value("speed").toDouble(1.0), 0.05, 20.0);
        if (!es.type.isEmpty())
            fx.effects << es;
    }
    if (o.contains("chroma")) {
        const QJsonObject c = o.value("chroma").toObject();
        fx.chroma.enabled = true;
        fx.chroma.color = col(c.value("color"), fx.chroma.color);
        fx.chroma.similarity = c.value("similarity").toDouble(fx.chroma.similarity);
        fx.chroma.smoothness = c.value("smoothness").toDouble(fx.chroma.smoothness);
        fx.chroma.spill = c.value("spill").toDouble(fx.chroma.spill);
    }
    if (o.contains("mask")) {
        const QJsonObject m = o.value("mask").toObject();
        fx.mask.type = m.value("type").toString();
        fx.mask.cx = m.value("cx").toDouble(0.5);
        fx.mask.cy = m.value("cy").toDouble(0.5);
        fx.mask.w = m.value("w").toDouble(0.6);
        fx.mask.h = m.value("h").toDouble(0.6);
        fx.mask.angle = m.value("angle").toDouble();
        fx.mask.feather = m.value("feather").toDouble(0.05);
        fx.mask.radius = m.value("radius").toDouble(0.1);
        fx.mask.invert = m.value("invert").toBool();
    }
    fx.blend = o.value("blend").toString("normal");
    if (o.contains("background")) {
        const QJsonObject b = o.value("background").toObject();
        fx.background.mode = b.value("mode").toString();
        fx.background.color = col(b.value("color"), Qt::black);
        fx.background.blur = b.value("blur").toDouble(0.6);
    }
    if (o.contains("adjust")) {
        const QJsonObject a = o.value("adjust").toObject();
        fx.adjust.hue = a.value("hue").toDouble();
        fx.adjust.vibrance = a.value("vibrance").toDouble();
        fx.adjust.highlights = a.value("highlights").toDouble();
        fx.adjust.shadows = a.value("shadows").toDouble();
        fx.adjust.fade = a.value("fade").toDouble();
        fx.adjust.vignette = a.value("vignette").toDouble();
        fx.adjust.grain = a.value("grain").toDouble();
        fx.adjust.sharpen = a.value("sharpen").toDouble();
        fx.adjust.lut = a.value("lut").toString();
        fx.adjust.lutStrength = a.value("lutStrength").toDouble(1.0);
    }
    fx.look = o.value("look").toString();
    if (o.contains("speedCurve")) {
        const QJsonObject s = o.value("speedCurve").toObject();
        fx.speedCurve.preset = s.value("preset").toString();
        for (const QJsonValue &v : s.value("points").toArray()) {
            const QJsonArray pt = v.toArray();
            if (pt.size() == 2)
                fx.speedCurve.points << QPointF(std::clamp(pt[0].toDouble(), 0.0, 1.0), std::clamp(pt[1].toDouble(1), 0.05, 20.0));
        }
        std::sort(fx.speedCurve.points.begin(), fx.speedCurve.points.end(),
                  [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    }
    if (o.contains("audioFx")) {
        const QJsonObject a = o.value("audioFx").toObject();
        fx.audio.voice = a.value("voice").toString();
        fx.audio.denoise = a.value("denoise").toBool();
        fx.audio.enhance = a.value("enhance").toBool();
    }
    return fx;
}

} // namespace ss
