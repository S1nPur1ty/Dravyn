// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QGuiApplication>
#include <QStyleHints>
#include <QWidget>
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
#include <QAccessibilityHints>
#endif

namespace UIAnimation {

inline bool IsEnabled(const QWidget*) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    if (QGuiApplication::styleHints()->accessibility()->motionPreference() ==
        Qt::MotionPreference::ReducedMotion) {
        return false;
    }
#endif
    // Native widget animation hints do not govern our custom library transitions.
    return true;
}

} // namespace UIAnimation
