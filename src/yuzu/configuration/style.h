// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QLayout>
#include <QScrollArea>
#include <QWidget>
#include "yuzu/util/design_system.h"

namespace ConfigurationShared {

inline QPixmap RoundedAvatar(const QPixmap& source) {
    QPixmap result(source.size());
    result.setDevicePixelRatio(source.devicePixelRatio());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF{}, source.deviceIndependentSize()), 16, 16);
    painter.setClipPath(clip);
    painter.drawPixmap(0, 0, source);
    return result;
}

inline QScrollArea* CreateScrollablePage(QWidget* page, QWidget* parent) {
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    if (page->layout()) {
        page->layout()->setSizeConstraint(QLayout::SetMinimumSize);
    }
    scroll->setWidget(page);
    page->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    return scroll;
}

inline void ApplyDialogStyle(QWidget* dialog) {
    UIControls::ApplyPalette(dialog);
    // Spin boxes own the frame; inset only their embedded editor text.
    for (auto* spin : dialog->findChildren<QAbstractSpinBox*>()) {
        if (auto* editor = spin->findChild<QLineEdit*>()) {
            const bool centered = spin->alignment().testFlag(Qt::AlignHCenter);
            editor->setTextMargins(centered ? 12 : 50, 0, 12, 0);
        }
    }
    for (auto* scroll : dialog->findChildren<QScrollArea*>()) {
        scroll->viewport()->setAutoFillBackground(false);
        if (scroll->widget()) {
            scroll->widget()->setAutoFillBackground(false);
        }
    }
    QFile style(QStringLiteral(":/library/settings.qss"));
    if (style.open(QIODevice::ReadOnly)) {
        dialog->setStyleSheet(UIControls::SharedStyle() + QString::fromUtf8(style.readAll()));
    }
}

} // namespace ConfigurationShared
