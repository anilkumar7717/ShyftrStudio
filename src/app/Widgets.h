#pragma once

#include "../core/Model.h"

#include <QColor>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

class QSlider;
class QDoubleSpinBox;

namespace ss {

// Colour swatch button with alpha support.
class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);
signals:
    void colorChanged(const QColor &c);

private:
    void updateLook();
    QColor m_color = Qt::white;
};

// Slider + spin box pair (keyboard accessible). Emits editingStarted/finished for undo merging.
class SliderSpin : public QWidget {
    Q_OBJECT
public:
    SliderSpin(double min, double max, double step, int decimals, const QString &suffix = {}, QWidget *parent = nullptr);
    double value() const;
    void setValue(double v); // does not emit
signals:
    void valueChanged(double v);

private:
    QSlider *m_slider;
    QDoubleSpinBox *m_spin;
    double m_min, m_step;
    bool m_block = false;
};

// Accepts "hh:mm:ss.mmm", "mm:ss.mmm", "ss.mmm" or frames like "120f".
class TimeEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit TimeEdit(QWidget *parent = nullptr);
    void setTime(TimeUs t);
    TimeUs time() const { return m_time; }
    void setFps(Rational fps) { m_fps = fps; }
    static bool parse(const QString &s, Rational fps, TimeUs *out);
signals:
    void timeEdited(ss::TimeUs t);

private:
    TimeUs m_time = 0;
    Rational m_fps{30, 1};
};

QWidget *hline(QWidget *parent = nullptr);

// Layout that wraps its items onto new rows when space runs out (responsive button rows).
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = 6, int vSpacing = 6);
    ~FlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

// A widget holding a FlowLayout, for convenient use inside box layouts.
QWidget *flowRow(const QList<QWidget *> &widgets, QWidget *parent = nullptr);

} // namespace ss
