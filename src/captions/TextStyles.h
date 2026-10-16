#pragma once

#include "../core/Model.h"

#include <functional>

namespace ss {

// Professional looks for text (titles, lower thirds, labels...). Applying a style changes
// only how the text looks (font, colours, outline, shadow, box, spacing, case...), never the
// words, position, size, alignment or animation, and only on the text it is applied to.
struct TextStyle {
    QString id;
    QString label;
    QString category; // Clean, Bold, Boxed, Glow, Elegant, Fun
    std::function<void(TitleProps &)> apply;
};

namespace TextStyles {
QVector<TextStyle> all();
const TextStyle *find(const QString &id);
// Resets the look to neutral, then applies the style. Returns false for an unknown id.
bool apply(TitleProps &tp, const QString &id);
// The look of `from` copied onto `to` (keeps to's text, position, size, alignment, animation).
void copyLook(const TitleProps &from, TitleProps &to);
} // namespace TextStyles

} // namespace ss
