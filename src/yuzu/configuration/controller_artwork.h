// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QPainter>
#include <QRadialGradient>

namespace ConfigurationShared {

inline void DrawControllerSticks(QPainter& painter, const std::array<QPointF, 2>& offsets = {},
                                 const std::array<bool, 2>& pressed = {}) {
    const std::array<QPointF, 2> centers{QPointF(378, 449), QPointF(986, 657)};
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    for (std::size_t i = 0; i < centers.size(); ++i) {
        const QPointF center = centers[i] + offsets[i] * 28;
        QRadialGradient cap(center - QPointF(20, 24), 100);
        cap.setColorAt(0, QColor(55, 55, 58));
        cap.setColorAt(1, QColor(29, 29, 32));
        painter.setPen(QPen(pressed[i] ? QColor(215, 211, 229) : QColor(65, 65, 69), 3));
        painter.setBrush(cap);
        painter.drawEllipse(center, 67, 67);
        painter.setPen(QPen(QColor(255, 255, 255, 13), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 54, 54);
    }
    painter.restore();
}

} // namespace ConfigurationShared
