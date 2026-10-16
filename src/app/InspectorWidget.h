#pragma once

#include "../core/Document.h"

#include <QScrollArea>
#include <functional>

class QFormLayout;
class QVBoxLayout;
class QGroupBox;
class QTabWidget;

namespace ss {

// Context-sensitive properties, organised in tabs like a mobile editor:
//   video  — Basic · Animate · Effects · Cutout · Speed
//   audio  — Audio · Voice · Speed
//   title  — Text · Style · Animate · Effects
//   shape  — Shape · Animate · Effects
//   effect layer — Effects
// Edits apply to every selected clip and merge into one undo step per control. Transform
// edits on keyframed clips write the keyframe at the playhead.
class InspectorWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit InspectorWidget(Document *doc, QWidget *parent = nullptr);
    void focusClip(const QString &clipId);
    void showTab(const QString &name);

signals:
    void reverseRequested(const QString &clipId);
    void freezeFrameRequested(const QString &clipId);
    void stabilizeRequested(const QString &clipId);
    void extractAudioRequested(const QString &clipId);
    void statusMessage(const QString &msg);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void rebuild();
    static void fitNarrow(QWidget *root);
    void refreshValues();
    QWidget *page(const QString &title);
    QGroupBox *section(const QString &title, QFormLayout **form);
    void edit(const QString &what, const QString &key, const std::function<void(Clip &, Track &)> &fn);
    // Edits the transform values (position/scale/rotation/opacity); keyframe-aware.
    void editAnimated(const QString &what, const QString &key, const std::function<void(AnimatedValues &)> &fn);
    const Clip *firstClip() const;

    void buildProject();
    void buildTransform(bool media);
    void buildCrop();
    void buildColour();
    void buildTransition();
    void buildKeyframes();
    void buildEffects(bool looks);
    void buildCutout();
    void buildSpeed(bool hasSpeed, bool video);
    void buildAudio();
    void buildVoice();
    void buildTitleText();
    void buildTitleStyle();
    void buildTitleAnimation();
    void buildShape();
    void buildFades(bool audio);

    Document *m_doc;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_layout = nullptr; // layout that section() adds to
    QTabWidget *m_tabs = nullptr;
    QString m_signature;
    QString m_lastTab;
    QVector<std::function<void()>> m_refreshers;
    bool m_updating = false;
    bool m_keyframed = false;
    bool m_wrapped = false; // form rows stacked (narrow panel)
};

} // namespace ss
