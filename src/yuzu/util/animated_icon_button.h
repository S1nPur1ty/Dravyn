// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHideEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QVariantAnimation>
#include "yuzu/util/animation.h"

class AnimatedIconButton final : public QPushButton {
public:
    explicit AnimatedIconButton(QWidget* parent = nullptr)
        : QPushButton(parent), rotation(this), background_animation(this) {
        connect(&rotation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) { DrawIcon(value.toReal()); });
        connect(&background_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) {
                    background = value.value<QColor>();
                    update();
                });
    }

    void SetAnimatedBackground(QColor normal, QColor hovered, QColor border) {
        normal_background = normal;
        hover_background = hovered;
        border_color = border;
        background = normal;
        animated_background = true;
        update();
    }

    void SetGlyph(const QIcon& icon) {
        glyph = icon;
        DrawIcon(angle);
    }

    void SetSpinning(bool active) {
        spinning = active;
        rotation.stop();
        DrawIcon(0);
        if (spinning && isVisible() && UIAnimation::IsEnabled(this)) {
            rotation.setEasingCurve(QEasingCurve::Linear);
            rotation.setDuration(1000);
            rotation.setLoopCount(-1);
            rotation.setStartValue(0.0);
            rotation.setEndValue(360.0);
            rotation.start();
        }
    }

    void SetExpanded(bool expanded) {
        rotation.stop();
        const qreal target = expanded ? 180.0 : 0.0;
        if (!UIAnimation::IsEnabled(this)) {
            DrawIcon(target);
            return;
        }
        rotation.setEasingCurve(QEasingCurve::OutCubic);
        rotation.setDuration(180);
        rotation.setLoopCount(1);
        rotation.setStartValue(angle);
        rotation.setEndValue(target);
        rotation.start();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        QPushButton::enterEvent(event);
        AnimateBackground(isEnabled() ? hover_background : normal_background);
    }

    void leaveEvent(QEvent* event) override {
        QPushButton::leaveEvent(event);
        AnimateBackground(normal_background);
    }

    void paintEvent(QPaintEvent* event) override {
        if (!animated_background) {
            QPushButton::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(background);
        painter.setPen(hasFocus() ? QColor(QStringLiteral("#90909c")) : border_color);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.drawRoundedRect(frame, frame.height() / 2, frame.height() / 2);
        const QRect icon_rect((width() - iconSize().width()) / 2,
                              (height() - iconSize().height()) / 2,
                              iconSize().width(), iconSize().height());
        icon().paint(&painter, icon_rect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }

    void showEvent(QShowEvent* event) override {
        QPushButton::showEvent(event);
        if (spinning) {
            SetSpinning(true);
        }
    }

    void hideEvent(QHideEvent* event) override {
        if (spinning) {
            rotation.stop();
        }
        QPushButton::hideEvent(event);
    }

private:
    void AnimateBackground(QColor target) {
        if (!animated_background) {
            return;
        }
        background_animation.stop();
        if (!UIAnimation::IsEnabled(this)) {
            background = target;
            update();
            return;
        }
        background_animation.setDuration(200);
        background_animation.setEasingCurve(QEasingCurve::InOutCubic);
        background_animation.setStartValue(background);
        background_animation.setEndValue(target);
        background_animation.start();
    }

    void DrawIcon(qreal value) {
        angle = value;
        const qreal scale = devicePixelRatioF();
        QPixmap image(iconSize() * scale);
        image.setDevicePixelRatio(scale);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(iconSize().width() / 2.0, iconSize().height() / 2.0);
        painter.rotate(angle);
        painter.translate(-iconSize().width() / 2.0, -iconSize().height() / 2.0);
        glyph.paint(&painter, QRect(QPoint(0, 0), iconSize()));
        painter.end();
        setIcon(QIcon(image));
    }

    QIcon glyph;
    QVariantAnimation rotation;
    QVariantAnimation background_animation;
    QColor normal_background;
    QColor hover_background;
    QColor border_color;
    QColor background;
    qreal angle = 0;
    bool spinning = false;
    bool animated_background = false;
};
