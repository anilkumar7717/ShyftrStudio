#include "Widgets.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSlider>
#include <cmath>

namespace ss {

ColorButton::ColorButton(QWidget *parent) : QPushButton(parent)
{
    setFixedSize(44, 24);
    setToolTip("Choose colour");
    connect(this, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_color, this, "Choose colour", QColorDialog::ShowAlphaChannel);
        if (c.isValid() && c != m_color) {
            setColor(c);
            emit colorChanged(c);
        }
    });
    updateLook();
}

void ColorButton::setColor(const QColor &c)
{
    m_color = c;
    updateLook();
}

void ColorButton::updateLook()
{
    const QString rgba = QString("rgba(%1,%2,%3,%4)").arg(m_color.red()).arg(m_color.green()).arg(m_color.blue()).arg(m_color.alphaF());
    setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid #666; border-radius: 4px; padding: 0; }").arg(rgba));
    setAccessibleName("Colour " + m_color.name(QColor::HexArgb));
}

SliderSpin::SliderSpin(double min, double max, double step, int decimals, const QString &suffix, QWidget *parent)
    : QWidget(parent), m_min(min), m_step(step)
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, int(std::lround((max - min) / step)));
    m_spin = new QDoubleSpinBox;
    m_spin->setRange(min, max);
    m_spin->setSingleStep(step);
    m_spin->setDecimals(decimals);
    m_spin->setSuffix(suffix);
    m_spin->setKeyboardTracking(false);
    m_spin->setMinimumWidth(70);
    m_slider->setMinimumWidth(40);
    l->addWidget(m_slider, 1);
    l->addWidget(m_spin);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_block)
            return;
        const double val = m_min + v * m_step;
        m_block = true;
        m_spin->setValue(val);
        m_block = false;
        emit valueChanged(val);
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_block)
            return;
        m_block = true;
        m_slider->setValue(int(std::lround((v - m_min) / m_step)));
        m_block = false;
        emit valueChanged(v);
    });
}

double SliderSpin::value() const { return m_spin->value(); }

void SliderSpin::setValue(double v)
{
    m_block = true;
    m_spin->setValue(v);
    m_slider->setValue(int(std::lround((v - m_min) / m_step)));
    m_block = false;
}

TimeEdit::TimeEdit(QWidget *parent) : QLineEdit(parent)
{
    setMaximumWidth(120);
    setToolTip("Time as hh:mm:ss.mmm, mm:ss.mmm, seconds, or frames (e.g. 120f)");
    connect(this, &QLineEdit::editingFinished, this, [this]() {
        TimeUs t;
        if (parse(text(), m_fps, &t)) {
            if (t != m_time) {
                m_time = t;
                emit timeEdited(t);
            }
        }
        setText(formatClock(m_time));
    });
}

void TimeEdit::setTime(TimeUs t)
{
    m_time = t;
    if (!hasFocus())
        setText(formatClock(t));
}

bool TimeEdit::parse(const QString &sIn, Rational fps, TimeUs *out)
{
    const QString s = sIn.trimmed();
    if (s.endsWith('f')) {
        bool ok = false;
        const qint64 f = s.chopped(1).toLongLong(&ok);
        if (!ok)
            return false;
        *out = framesToUs(f, fps);
        return true;
    }
    static const QRegularExpression re(R"(^(?:(\d+):)?(?:(\d+):)?(\d+(?:[.,]\d+)?)$)");
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    double secs = QString(m.captured(3)).replace(',', '.').toDouble();
    if (!m.captured(2).isEmpty()) {
        secs += m.captured(2).toDouble() * 60 + m.captured(1).toDouble() * 3600;
    } else if (!m.captured(1).isEmpty()) {
        secs += m.captured(1).toDouble() * 60;
    }
    *out = TimeUs(std::llround(secs * kUsPerSec));
    return true;
}

QWidget *hline(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Plain);
    f->setStyleSheet("color: #2E323B;");
    return f;
}

} // namespace ss

namespace ss {

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing) : QLayout(parent), m_h(hSpacing), m_v(vSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }
int FlowLayout::count() const { return m_items.size(); }
QLayoutItem *FlowLayout::itemAt(int index) const { return m_items.value(index); }
QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}
int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }
void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}
QSize FlowLayout::sizeHint() const
{
    // prefer a single row
    int w = 0, h = 0;
    for (QLayoutItem *i : m_items) {
        w += i->sizeHint().width() + m_h;
        h = std::max(h, i->sizeHint().height());
    }
    return QSize(std::max(0, w - m_h), h);
}
QSize FlowLayout::minimumSize() const
{
    QSize s;
    for (QLayoutItem *i : m_items)
        s = s.expandedTo(i->minimumSize());
    const QMargins m = contentsMargins();
    return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    for (QLayoutItem *item : m_items) {
        if (item->widget() && item->widget()->isHidden())
            continue;
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_h;
        if (nextX - m_h > r.right() + 1 && lineHeight > 0) {
            x = r.x();
            y += lineHeight + m_v;
            nextX = x + hint.width() + m_h;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = std::max(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

QWidget *flowRow(const QList<QWidget *> &widgets, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *f = new FlowLayout(w);
    for (QWidget *c : widgets)
        f->addWidget(c);
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    w->setSizePolicy(sp);
    return w;
}

} // namespace ss
