// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <QComboBox>
#include <QListView>
#include <QFile>
#include <QMenu>
#include <QPainterPath>
#include <QPainter>
#include <QScreen>
#include <QAbstractItemView>
#include <QResizeEvent>
#include <QStyleOptionComboBox>
#include <QStyledItemDelegate>
#include "yuzu/util/animated_icon_button.h"

namespace UIControls {
class RoundedPopup final : public QWidget {
public:
    explicit RoundedPopup(QWidget* parent = nullptr) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#42424c")), 1));
        painter.setBrush(QColor(QStringLiteral("#26262c")));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
    }
};

inline QPoint PopupPosition(const QWidget* anchor, QSize size, bool below) {
    const QRect bounds = anchor->screen()->availableGeometry();
    const QPoint top = anchor->mapToGlobal(QPoint(0, 0));
    const int bottom = top.y() + anchor->height();
    int y = below ? bottom + 6 : top.y() - size.height() - 6;
    if (below && y + size.height() > bounds.bottom() + 1) {
        y = top.y() - size.height() - 6;
    } else if (!below && y < bounds.top()) {
        y = bottom + 6;
    }
    return {std::clamp(top.x(), bounds.left(), std::max(bounds.left(), bounds.right() + 1 - size.width())),
            std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - size.height()))};
}

inline void RoundPopup(QWidget* popup) {
    QPainterPath shape;
    shape.addRoundedRect(QRectF(popup->rect()), 12, 12);
    popup->setMask(QRegion(shape.toFillPolygon().toPolygon()));
}

inline const QString& PopupStyle() {
    static const QString style = [] {
        QFile file(QStringLiteral(":/library/popups.qss"));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
    }();
    return style;
}

inline void ExecStatusMenu(QMenu& menu, QWidget* button) {
    menu.setObjectName(QStringLiteral("StatusMenu"));
    menu.setStyleSheet(PopupStyle());
    menu.ensurePolished();
    menu.adjustSize();
    RoundPopup(&menu);
    menu.exec(PopupPosition(button, menu.sizeHint(), false));
}

class ComboBox final : public QComboBox {
public:
    explicit ComboBox(QWidget* parent = nullptr) : QComboBox(parent) {
        setView(new QListView(this));
        // Use item-view styling instead of the platform's combo menu delegate.
        setItemDelegate(new QStyledItemDelegate(view()));
        view()->setMouseTracking(true);
        setMaxVisibleItems(12);
        arrow = new AnimatedIconButton(this);
        arrow->setObjectName(QStringLiteral("LibraryDropdownArrow"));
        arrow->setIconSize(QSize(16, 16));
        arrow->SetGlyph(QIcon(QStringLiteral(":/library/chevron-down.svg")));
        arrow->setFocusPolicy(Qt::NoFocus);
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void showPopup() override {
        QComboBox::showPopup();
        auto* popup = view()->window();
        if (!popup->isVisible()) {
            return;
        }
        popup->installEventFilter(this);
        arrow->SetExpanded(true);
        const int width = std::min(std::max(this->width(), popup->width()),
                                   screen()->availableGeometry().width());
        popup->resize(width, popup->height());
        RoundPopup(popup);
        popup->move(PopupPosition(this, popup->size(), true));
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QComboBox::resizeEvent(event);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QRect area = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                   QStyle::SC_ComboBoxArrow, this);
        arrow->setGeometry(area.center().x() - 12, (height() - 28) / 2, 24, 28);
    }

    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == view()->window() && event->type() == QEvent::Hide) {
            arrow->SetExpanded(false);
        }
        return QComboBox::eventFilter(object, event);
    }

private:
    AnimatedIconButton* arrow;
};
using LibraryComboBox = ComboBox;
} // namespace UIControls
