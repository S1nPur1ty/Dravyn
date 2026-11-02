// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QPainter>
#include <QPainterPath>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect card = getCardRect(option, index);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const auto palette = option.palette;
    const int cover_width = std::max(1, card.width() - 16);
    const QRect cover(card.left() + 8, card.top() + 8, cover_width, cover_width);
    const QString title = index.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
    const auto picture = index.data(Qt::DecorationRole).value<QPixmap>();

    painter->save();
    QPainterPath clip;
    clip.addRoundedRect(cover, 24, 24);
    painter->setClipPath(clip);
    if (!picture.isNull()) {
        // Fill the square tile while preserving the artwork aspect ratio.
        painter->fillRect(cover, palette.alternateBase());
        QSize size = picture.size();
        size.scale(cover.size(), Qt::KeepAspectRatioByExpanding);
        QRect target(QPoint(), size);
        target.moveCenter(cover.center());
        painter->drawPixmap(target, picture);
    } else {
        painter->fillRect(cover, QColor(40, 40, 47));
        QString initials;
        for (const auto& word : title.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            initials += word.front().toUpper();
            if (initials.size() == 2) {
                break;
            }
        }
        QFont font = option.font;
        font.setPixelSize(std::max(24, cover_width / 3));
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(cover, Qt::AlignCenter, initials);
    }
    painter->restore();
    if (selected || hovered || (option.state & QStyle::State_HasFocus)) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(selected ? QColor(205, 199, 226) : QColor(110, 110, 124), 2));
        painter->drawRoundedRect(cover.adjusted(-3, -3, 3, 3), 26, 26);
    }

    if (UISettings::values.show_game_name) {
        QFont font = option.font;
        font.setPixelSize(13);
        font.setWeight(QFont::Medium);
        painter->setFont(font);
        painter->setPen(QColor(232, 232, 239));
        const QRect text_rect(card.left() + 12, cover.bottom() + 12, card.width() - 24, 38);
        const auto metrics = painter->fontMetrics();
        // Reserve two lines; long titles remain available in the tooltip and accessibility text.
        QString first = title;
        QString second;
        if (metrics.horizontalAdvance(title) > text_rect.width()) {
            int split = title.lastIndexOf(QLatin1Char(' '));
            while (split > 0 && metrics.horizontalAdvance(title.left(split)) > text_rect.width()) {
                split = title.lastIndexOf(QLatin1Char(' '), split - 1);
            }
            if (split > 0) {
                first = title.left(split);
                second = title.mid(split + 1);
            }
        }
        painter->drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop,
                          metrics.elidedText(first, Qt::ElideRight, text_rect.width()));
        if (!second.isEmpty()) {
            painter->drawText(text_rect.translated(0, metrics.height()), Qt::AlignHCenter | Qt::AlignTop,
                              metrics.elidedText(second, Qt::ElideRight, text_rect.width()));
        }
    }
    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex&) const {
    const int width = option.rect.width() - m_padding - cardMargin * 2;
    const int height = std::min(option.rect.height(), m_contentSize.height()) - cardMargin * 2;
    return QRect(option.rect.left() + (option.rect.width() - width) / 2,
                 option.rect.top() + (m_columns > 0 ? cardMargin : (option.rect.height() - height) / 2),
                 width, height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, int padding, int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
