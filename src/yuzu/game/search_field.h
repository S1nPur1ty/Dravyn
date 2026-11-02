// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class GameList;
class QGraphicsOpacityEffect;
class QLineEdit;
class QToolButton;
class QVariantAnimation;

class GameListSearchField : public QWidget {
    Q_OBJECT

public:
    explicit GameListSearchField(GameList* parent = nullptr);

    QString filterText() const;
    void setFilterResult(int visible, int total);
    void clear();
    void setFocus();

private:
    bool eventFilter(QObject* object, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void SetExpanded(bool expanded);
    void RetranslateUI();

    GameList* game_list;
    QLineEdit* edit_filter;
    QToolButton* search_button;
    QVariantAnimation* width_animation;
    QGraphicsOpacityEffect* text_opacity;
    int visible_results = 0;
    bool expanded = false;
};
