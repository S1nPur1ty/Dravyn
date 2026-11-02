// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QWidget>
#include "yuzu/util/animation.h"

namespace UIAnimation {

// Fade through the page background so outgoing and incoming labels never overlap.
class PageTransition final : public QWidget {
public:
    explicit PageTransition(QWidget* parent) : QWidget(parent), animation(this) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAutoFillBackground(false);
        hide();
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            progress = value.toReal();
            update();
        });
        connect(&animation, &QVariantAnimation::finished, this, [this] { Clear(); });
    }

    void Capture() {
        if (!parentWidget()->isVisible() || !IsEnabled(this)) {
            Clear();
            return;
        }
        // Rapid navigation continues from the currently displayed frame, not a hidden page.
        outgoing = isVisible() ? grab() : CapturePage();
        animation.stop();
        hide();
        incoming = {};
        progress = 0.0;
    }

    void Start() {
        if (outgoing.isNull()) {
            return;
        }
        animation.stop();
        hide();
        if (auto* layout = parentWidget()->layout()) {
            layout->activate();
        }
        incoming = CapturePage();
        setGeometry(parentWidget()->rect());
        progress = 0.0;
        raise();
        show();
        animation.setDuration(300);
        animation.setEasingCurve(QEasingCurve::Linear);
        animation.setStartValue(0.0);
        animation.setEndValue(1.0);
        animation.start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        // An opaque base also prevents the live incoming page bleeding through the overlay.
        painter.fillRect(rect(), Background());
        const bool first_half = progress < 0.5;
        const qreal phase = first_half ? 1.0 - progress * 2.0 : progress * 2.0 - 1.0;
        painter.setOpacity(QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(phase));
        painter.drawPixmap(0, 0, first_half ? outgoing : incoming);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (animation.state() == QAbstractAnimation::Running) {
            Clear();
        }
    }

private:
    QColor Background() const {
        return window()->palette().color(QPalette::Window);
    }

    QPixmap CapturePage() {
        auto* page = parentWidget();
        QPixmap frame((QSizeF(page->size()) * page->devicePixelRatioF()).toSize());
        frame.setDevicePixelRatio(page->devicePixelRatioF());
        frame.fill(Background());
        page->render(&frame, QPoint{}, QRegion{}, QWidget::DrawChildren);
        return frame;
    }

    void Clear() {
        animation.stop();
        hide();
        outgoing = {};
        incoming = {};
    }

    QPixmap outgoing;
    QPixmap incoming;
    QVariantAnimation animation;
    qreal progress{};
};

} // namespace UIAnimation
