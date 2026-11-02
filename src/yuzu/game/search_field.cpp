// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>

#include "game/game_list.h"
#include "game/search_field.h"
#include "yuzu/util/animation.h"

namespace {
constexpr int CollapsedWidth = 40;
constexpr int ExpandedWidth = 264;
} // namespace

GameListSearchField::GameListSearchField(GameList* parent)
    : QWidget{parent}, game_list{parent}, edit_filter{new QLineEdit(this)},
      search_button{new QToolButton(this)}, width_animation{new QVariantAnimation(this)},
      text_opacity{new QGraphicsOpacityEffect(this)} {
    setObjectName(QStringLiteral("LibrarySearch"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedSize(CollapsedWidth, 40);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 10, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);

    search_button->setObjectName(QStringLiteral("LibrarySearchButton"));
    search_button->setIcon(QIcon(QStringLiteral(":/library/search.svg")));
    search_button->setFixedSize(38, 38);
    search_button->setFocusPolicy(Qt::StrongFocus);
    search_button->installEventFilter(this);
    layout->addWidget(search_button);

    edit_filter->setMinimumWidth(0);
    edit_filter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    edit_filter->setClearButtonEnabled(true);
    edit_filter->installEventFilter(this);
    edit_filter->setGraphicsEffect(text_opacity);
    edit_filter->hide();
    layout->addWidget(edit_filter, 1);

    width_animation->setDuration(200);
    width_animation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(width_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const int width = value.toInt();
        setFixedWidth(width);
        text_opacity->setOpacity(qBound(0.0, double(width - CollapsedWidth) /
                                               (ExpandedWidth - CollapsedWidth), 1.0));
    });
    connect(width_animation, &QVariantAnimation::finished, this,
            [this] { edit_filter->setVisible(expanded); });
    connect(search_button, &QToolButton::clicked, this, &GameListSearchField::setFocus);
    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::OnTextChanged);
    RetranslateUI();
}

void GameListSearchField::SetExpanded(bool expand) {
    if (expanded == expand) {
        return;
    }
    expanded = expand;
    width_animation->stop();
    if (expand) {
        edit_filter->show();
    }
    const int target = expand ? ExpandedWidth : CollapsedWidth;
    if (!isVisible() || !UIAnimation::IsEnabled(this)) {
        setFixedWidth(target);
        text_opacity->setOpacity(expand ? 1.0 : 0.0);
        edit_filter->setVisible(expand);
        return;
    }
    width_animation->setStartValue(width());
    width_animation->setEndValue(target);
    width_animation->start();
}

void GameListSearchField::setFilterResult(int visible, int total) {
    visible_results = visible;
    edit_filter->setAccessibleDescription(tr("%1 of %n result(s)", "", total).arg(visible));
}

QString GameListSearchField::filterText() const {
    return edit_filter->text();
}

void GameListSearchField::clear() {
    edit_filter->clear();
    if (!edit_filter->hasFocus() && !search_button->hasFocus()) {
        SetExpanded(false);
    }
}

void GameListSearchField::setFocus() {
    SetExpanded(true);
    edit_filter->setFocus(Qt::ShortcutFocusReason);
}

bool GameListSearchField::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return ||
            key->key() == Qt::Key_Enter) {
            event->accept();
            return true;
        }
    } else if (event->type() == QEvent::FocusIn) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        if (object == edit_filter) {
            SetExpanded(true);
        } else if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason) {
            // Mouse activation expands on release, so the button never moves under the click.
            // Keyboard navigation enters the editor directly instead of stopping on its icon.
            setFocus();
        }
    } else if (event->type() == QEvent::FocusOut) {
        // Wait for focus to reach the other control before deciding to collapse.
        QTimer::singleShot(0, this, [this] {
            if (!edit_filter->hasFocus() && !search_button->hasFocus() &&
                edit_filter->text().isEmpty()) {
                SetExpanded(false);
            }
        });
    } else if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            edit_filter->clear();
            game_list->setFocus();
            SetExpanded(false);
            return true;
        }
        if (object == edit_filter &&
            (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            if (visible_results == 1 && !game_list->item_model->IsPopulating()) {
                const auto path = game_list->GetLastFilterResultItem();
                edit_filter->clear();
                if (!path.isEmpty()) {
                    emit game_list->GameChosen(path);
                }
            }
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void GameListSearchField::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void GameListSearchField::RetranslateUI() {
    search_button->setAccessibleName(tr("Search games"));
    search_button->setToolTip(tr("Search games"));
    edit_filter->setAccessibleName(tr("Search games"));
    edit_filter->setPlaceholderText(tr("Search games"));
}
