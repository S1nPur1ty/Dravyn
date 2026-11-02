// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTreeView>

class RoomList final : public QTreeView {
public:
    using QTreeView::QTreeView;

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
        if (selectionModel()->isRowSelected(index.row(), index.parent())) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(QStringLiteral("#383541")));
            const QRectF highlight(2, option.rect.top() + 2, viewport()->width() - 4,
                                   option.rect.height() - 4);
            painter->drawRoundedRect(highlight, 10, 10);
            painter->restore();
        }
        QTreeView::drawRow(painter, option, index);
    }
};
