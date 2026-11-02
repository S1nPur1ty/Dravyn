// SPDX-License-Identifier: GPL-3.0-or-later

#include <QEvent>
#include <QFile>
#include <QPainter>
#include <QPointer>
#include <QStyleOptionButton>
#include "yuzu/util/animation.h"
#include "yuzu/util/design_system.h"

namespace UIControls {

namespace {
class EnabledBinding final : public QObject {
public:
    EnabledBinding(QWidget* source_, QWidget* target_)
        : QObject(target_), source(source_), target(target_) {
        target->setEnabled(source->isEnabled());
        source->installEventFilter(this);
    }

private:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == source && event->type() == QEvent::EnabledChange && target) {
            target->setEnabled(source->isEnabled());
        }
        return QObject::eventFilter(object, event);
    }
    QPointer<QWidget> source;
    QPointer<QWidget> target;
};
} // namespace

void BindEnabled(QWidget* source, QWidget* target) {
    new EnabledBinding(source, target);
}

Button::Button(QWidget* parent) : QPushButton(parent), hover_animation(this) {
    connect(&hover_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                hover_amount = value.toReal();
                update();
            });
}

Button::Button(const QString& text, QWidget* parent) : Button(parent) {
    setText(text);
}

Button::Button(const QIcon& icon, const QString& text, QWidget* parent) : Button(text, parent) {
    setIcon(icon);
}

void Button::AnimateHover(qreal target) {
    hover_animation.stop();
    if (!UIAnimation::IsEnabled(this)) {
        hover_amount = target;
        update();
        return;
    }
    hover_animation.setDuration(160);
    hover_animation.setEasingCurve(QEasingCurve::OutCubic);
    hover_animation.setStartValue(hover_amount);
    hover_animation.setEndValue(target);
    hover_animation.start();
}

void Button::enterEvent(QEnterEvent* event) {
    QPushButton::enterEvent(event);
    AnimateHover(1.0);
}

void Button::leaveEvent(QEvent* event) {
    QPushButton::leaveEvent(event);
    AnimateHover(0.0);
}

void Button::paintEvent(QPaintEvent*) {
    QStyleOptionButton option;
    initStyleOption(&option);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool primary = isDefault();
    QColor background = primary ? Colors::Accent : Colors::Control;
    if (isFlat()) {
        background = Qt::transparent;
    }
    painter.setOpacity(isEnabled() ? 1.0 : 0.45);
    const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(hasFocus() ? QPen(Colors::Accent, 1) : QPen(Qt::NoPen));
    painter.setBrush(background);
    painter.drawRoundedRect(bounds, 11, 11);
    painter.setPen(Qt::NoPen);
    QColor overlay = primary ? QColor(Qt::white) : Colors::Accent;
    overlay.setAlphaF(isDown() || (isChecked() && !isFlat()) ? 0.18 : hover_amount * 0.09);
    painter.setBrush(overlay);
    painter.drawRoundedRect(bounds, 11, 11);
    option.palette.setColor(QPalette::ButtonText, primary ? Colors::Background : Colors::Text);
    style()->drawControl(QStyle::CE_PushButtonLabel, &option, &painter, this);
}

const QString& SharedStyle() {
    static const QString style = [] {
        QFile file(QStringLiteral(":/library/controls.qss"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
    }();
    return style;
}

void ApplyPalette(QWidget* widget) {
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, Colors::Background);
    palette.setColor(QPalette::WindowText, Colors::Text);
    palette.setColor(QPalette::Base, Colors::Surface);
    palette.setColor(QPalette::AlternateBase, Colors::Surface);
    palette.setColor(QPalette::Text, Colors::Text);
    palette.setColor(QPalette::Button, Colors::Control);
    palette.setColor(QPalette::ButtonText, Colors::Text);
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#45404f")));
    palette.setColor(QPalette::HighlightedText, Colors::Text);
    palette.setColor(QPalette::Disabled, QPalette::Text, Colors::Muted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, Colors::Muted);
    widget->setPalette(palette);
}

} // namespace UIControls
