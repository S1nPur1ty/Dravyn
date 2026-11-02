// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QScroller>
#include <QScrollerProperties>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/common.h"
#include "yuzu/game/game_card.h"
#include "yuzu/game/game_grid.h"

GameGrid::GameGrid(QWidget* parent) : QListView{parent} {
    m_gameCard = new GameCard(this);
    setItemDelegate(m_gameCard);

    setViewMode(QListView::ListMode);
    setResizeMode(QListView::Fixed);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);

    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setSpacing(0);
    setWordWrap(true);
    setTextElideMode(Qt::ElideRight);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
}

void GameGrid::SetModel(GameListModel* model) {
    QListView::setModel(model);
    UpdateIconSize();
}

void GameGrid::ApplyFilter(const QString& edit_filter_text, GameListModel* model) {
    int row_count = model->rowCount();

    for (int i = 0; i < row_count; ++i) {
        QStandardItem* item = model->item(i, 0);
        if (!item)
            continue;

        if (Yuzu::FilterMatches(edit_filter_text, item)) {
            setRowHidden(i, false);
        } else {
            setRowHidden(i, true);
        }
    }
}

void GameGrid::UpdateIconSize() {
    const int icon_size = std::clamp<int>(UISettings::values.game_icon_size.GetValue(), 128, 320);
    const int width = std::max(1, viewport()->width());
    const int columns = std::max(1, width / (icon_size + 32));
    const int cell_width = width / columns;
    const int card_width = std::min(cell_width, icon_size + 32);
    const int height = card_width + (UISettings::values.show_game_name ? 54 : 0);
    const QSize cell_size(cell_width, height);
    setGridSize(cell_size);
    m_gameCard->setSize(cell_size, cell_size, cell_width - card_width, columns);
    viewport()->update();
}

QModelIndex GameGrid::indexAt(const QPoint& point) const {
    QModelIndex index = QListView::indexAt(point);
    if (!index.isValid())
        return {};
    if (m_gameCard && !m_gameCard->hitTest(point, index, this, visualRect(index)))
        return {};
    return index;
}
