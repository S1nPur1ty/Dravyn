// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHideEvent>
#include <QPainter>
#include <QShowEvent>
#include <QVariantAnimation>
#include <QWidget>
#include "yuzu/util/animation.h"

class LoadingSpinner final : public QWidget {
public:
    explicit LoadingSpinner(QWidget* parent = nullptr) : QWidget(parent), rotation(this) {
        setFixedSize(28, 28);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAccessibleName(tr("Loading"));
        rotation.setStartValue(0.0);
        rotation.setEndValue(360.0);
        rotation.setDuration(1000);
        rotation.setLoopCount(-1);
        connect(&rotation, &QVariantAnimation::valueChanged, this, [this] { update(); });
        hide();
    }

    void SetActive(bool loading) {
        active = loading;
        setVisible(loading);
        UpdateAnimation();
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        UpdateAnimation();
    }

    void hideEvent(QHideEvent* event) override {
        rotation.stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF ring = QRectF(rect()).adjusted(3, 3, -3, -3);
        painter.setPen(QPen(QColor(QStringLiteral("#42424a")), 2.5));
        painter.drawEllipse(ring);
        painter.setPen(QPen(QColor(QStringLiteral("#c4bedb")), 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(ring, static_cast<int>((90 - rotation.currentValue().toDouble()) * 16),
                        -100 * 16);
    }

private:
    void UpdateAnimation() {
        if (active && isVisible() && UIAnimation::IsEnabled(this)) {
            if (rotation.state() != QAbstractAnimation::Running) {
                rotation.start();
            }
        } else {
            rotation.stop();
        }
    }

    QVariantAnimation rotation;
    bool active = false;
};
