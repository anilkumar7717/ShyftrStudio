#pragma once

#include "../core/Document.h"

#include <QFrame>

class QFontComboBox;
class QSpinBox;
class QToolButton;
class QComboBox;

namespace ss {

class ColorButton;

// Floating formatting bar shown over the video while one text is selected (like a word
// processor's). Every change applies to that text only, never to other texts.
class TextToolbar : public QFrame {
    Q_OBJECT
public:
    TextToolbar(Document *doc, QWidget *parent = nullptr);
    void setClip(const QString &clipId); // empty hides it
    QString clipId() const { return m_clip; }
    void refresh();

signals:
    void editTextRequested(const QString &clipId);

private:
    void change(const QString &what, const QString &key, const std::function<void(TitleProps &)> &fn);
    Document *m_doc;
    QString m_clip;
    bool m_updating = false;
    QFontComboBox *m_font;
    QSpinBox *m_size;
    QToolButton *m_bold, *m_italic, *m_underline, *m_case, *m_align, *m_styles;
    ColorButton *m_color, *m_highlight;
};

} // namespace ss
