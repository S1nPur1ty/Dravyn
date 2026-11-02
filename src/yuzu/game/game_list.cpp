// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QComboBox>
#include <QFileDialog>
#include <QImageReader>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QStackedLayout>
#include <QMessageBox>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QListView>
#include <QMenu>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QToolButton>
#include <QVariantAnimation>
#include <qlayoutitem.h>

#include "common/common_types.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"
#include "yuzu/game/carousel.h"
#include "yuzu/game/common.h"
#include "yuzu/game/game_grid.h"
#include "yuzu/game/game_list.h"
#include "yuzu/game/game_tree.h"
#include "yuzu/game/search_field.h"
#include "yuzu/main_window.h"
#include "yuzu/util/controller_navigation.h"
#include "yuzu/util/animation.h"
#include "yuzu/util/animated_icon_button.h"
#include "yuzu/util/loading_spinner.h"
#include "yuzu/util/popup.h"
#include "yuzu/util/design_system.h"

GameList::GameList(FileSys::VirtualFilesystem vfs_, FileSys::ManualContentProvider* provider_,
                   PlayTime::PlayTimeManager& play_time_manager_, Core::System& system_,
                   MainWindow* parent)
    : QWidget{parent}, vfs{std::move(vfs_)}, provider{provider_},
      play_time_manager{play_time_manager_}, system{system_} {

    this->main_window = parent;
    setObjectName(QStringLiteral("GameLibrary"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::ClickFocus);
    QFile library_style(QStringLiteral(":/library/style.qss"));
    if (library_style.open(QIODevice::ReadOnly)) {
        setStyleSheet(QString::fromUtf8(library_style.readAll()));
    }
    layout = new QVBoxLayout;
    layout->setContentsMargins(32, 24, 32, 12);
    layout->setSpacing(0);
    setLayout(layout);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    item_model = new GameListModel(vfs, provider, play_time_manager, system, this);

    SetupViews();

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("GameLibraryHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(8, 0, 8, 28);
    header_layout->setSpacing(10);
    auto* logo = new QLabel(header);
    logo->setPixmap(QIcon(QStringLiteral(":/img/dravyn.png")).pixmap(30, 30));
    header_layout->addWidget(logo);
    auto* brand = new QLabel(QStringLiteral("Dravyn"), header);
    brand->setObjectName(QStringLiteral("LibraryBrand"));
    header_layout->addWidget(brand);
    header_layout->addStretch();
    search_field = new GameListSearchField(this);
    header_layout->addWidget(search_field);
    folder_filter = new UIControls::LibraryComboBox(header);
    folder_filter->setMaximumWidth(220);
    folder_filter->setFixedHeight(40);
    folder_filter->setView(new QListView(folder_filter));
    folder_filter->setAccessibleName(tr("Game folders"));
    header_layout->addWidget(folder_filter);
    sort_order = new UIControls::LibraryComboBox(header);
    sort_order->setFixedHeight(40);
    sort_order->setView(new QListView(sort_order));
    sort_order->addItems({tr("Recently played"), tr("Name A–Z"), tr("Name Z–A")});
    sort_order->setAccessibleName(tr("Sort games"));
    connect(sort_order, &QComboBox::currentIndexChanged, this, &GameList::ApplySort);
    header_layout->addWidget(sort_order);
    auto* settings = new AnimatedIconButton(header);
    settings->setObjectName(QStringLiteral("LibrarySettings"));
    settings->SetGlyph(QIcon(QStringLiteral(":/library/settings.svg")));
    settings->setIconSize(QSize(18, 18));
    settings->setFixedSize(40, 40);
    settings->setAccessibleName(tr("Settings"));
    settings->setToolTip(tr("Settings"));
    settings->SetAnimatedBackground(UIControls::Colors::Control,
                                    UIControls::Colors::Hover,
                                    UIControls::Colors::Control);
    connect(settings, &QPushButton::clicked, this, &GameList::SettingsRequested);
    header_layout->addWidget(settings);
    refresh_games = new AnimatedIconButton(header);
    refresh_games->setObjectName(QStringLiteral("LibraryRefresh"));
    refresh_games->setIconSize(QSize(18, 18));
    refresh_games->SetGlyph(QIcon(QStringLiteral(":/library/refresh.svg")));
    refresh_games->setAccessibleName(tr("Refresh games"));
    refresh_games->setToolTip(tr("Refresh games"));
    refresh_games->setFixedSize(40, 40);
    refresh_games->SetAnimatedBackground(UIControls::Colors::Control,
                                         UIControls::Colors::Hover,
                                         UIControls::Colors::Control);
    connect(refresh_games, &QPushButton::clicked, this, &GameList::RefreshRequested);
    header_layout->addWidget(refresh_games);
    layout->addWidget(header);

    empty_results = new QLabel(tr("No games match your search."), this);
    empty_results->setAlignment(Qt::AlignCenter);
    empty_results->hide();
    layout->addWidget(empty_results);

    auto* content = new QWidget(this);
    auto* content_layout = new QGridLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    auto* views = new QWidget(content);
    content_stack = new QStackedLayout(views);
    content_layout->addWidget(views, 0, 0);
    auto* add = new AnimatedIconButton(content);
    add_games = add;
    add->setObjectName(QStringLiteral("LibraryAdd"));
    add->SetGlyph(QIcon(QStringLiteral(":/library/plus.svg")));
    add->setAccessibleName(tr("Add games"));
    add->setToolTip(tr("Add games"));
    add->setFixedSize(48, 48);
    add->setIconSize(QSize(22, 22));
    add->SetAnimatedBackground(QColor(QStringLiteral("#29292f")),
                               QColor(QStringLiteral("#3a3744")),
                               QColor(QStringLiteral("#48434f")));
    connect(add, &QPushButton::clicked, this, &GameList::AddDirectory);
    content_layout->addWidget(add, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    add->raise();
    content_stack->setContentsMargins(0, 0, 0, 0);
    loading_page = new QWidget(content);
    auto* loading_layout = new QVBoxLayout(loading_page);
    loading_layout->setContentsMargins(0, 0, 0, 0);
    loading_spinner = new LoadingSpinner(loading_page);
    loading_spinner->setObjectName(QStringLiteral("LibraryLoadingSpinner"));
    loading_spinner->setAccessibleName(tr("Loading games"));
    loading_layout->addStretch();
    loading_layout->addWidget(loading_spinner, 0, Qt::AlignHCenter);
    loading_layout->addStretch();
    content_stack->addWidget(loading_page);
    content_stack->addWidget(tree_view);
    content_stack->addWidget(grid_view);
    content_stack->addWidget(carousel_view);
    layout->addWidget(content, 1);
    loading_spinner->SetActive(true);
    connect(folder_filter, &QComboBox::currentIndexChanged, this,
            [this] { OnTextChanged(search_field->filterText()); });

    controller_navigation = new ControllerNavigation(system.HIDCore(), this);

    SetupScrollAnimation();

    connect(tree_view, &QTreeView::activated, this, &GameList::ValidateEntry);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(grid_view, &QListView::activated, this, &GameList::ValidateEntry);
    connect(grid_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(carousel_view, &QListView::activated, this, &GameList::ValidateEntry);
    connect(carousel_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent, this,
            [this](Qt::Key key) {
                if (system.IsPoweredOn()) {
                    return;
                }
                if (!this->isActiveWindow()) {
                    return;
                }
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                QCoreApplication::postEvent(m_currentView, event);
            });

    connect(item_model, &GameListModel::PopulatingCompleted, this,
            &GameList::OnPopulatingCompleted);

    connect(item_model, &GameListModel::ShowList, this, &GameList::ShowList);
    connect(item_model, &GameListModel::SaveConfig, this, &GameList::SaveConfig);
    connect(item_model, &GameListModel::PopulatingStarted, this, &GameList::OnPopulate);
    connect(item_model, &GameListModel::SnapshotAvailable, this, [this] {
        library_ready = true;
        m_currentView->setEnabled(true);
        loading_spinner->SetActive(false);
        UpdateLibraryFolders();
        ApplySort();
        AnimateGames();
    });

    // TODO: impl on grid/carousel
    connect(tree_view, &GameTree::FilterResultReady, search_field,
            [this](int visible, int total) { search_field->setFilterResult(visible, total); });

    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    ResetViewMode();
    item_model->RestoreSnapshot(UISettings::values.game_dirs);
}

GameList::~GameList() {
    UnloadController();
}

void GameList::SetupViews() {
    tree_view = new GameTree(this);
    grid_view = new GameGrid(this);
    carousel_view = new GameCarousel(this);

    tree_view->SetModel(item_model);
    grid_view->SetModel(item_model);
    carousel_view->SetModel(item_model);
}

QString GameList::GetLastFilterResultItem() const {
    if (m_isTreeMode) {
        return tree_view->GetLastFilterResultItem();
    }
    const auto* view = static_cast<QListView*>(m_currentView);
    for (int row = 0; row < item_model->rowCount(); ++row) {
        if (!view->isRowHidden(row)) {
            auto path = item_model->index(row, 0).data(GameListItemPath::FullPathRole).toString();
            if (QFileInfo(path).isDir()) {
                path = QDir(path).filePath(QStringLiteral("main"));
            }
            return path;
        }
    }
    return {};
}

void GameList::ClearFilter() {
    search_field->clear();
}

void GameList::SetFilterFocus() {
    if (item_model->rowCount() > 0) {
        search_field->setFocus();
    }
}

void GameList::SetFilterVisible(bool visibility) {
    search_field->setVisible(visibility);
}

bool GameList::IsEmpty() const {
    return item_model->IsEmpty();
}

void GameList::OnPopulate() {
    refresh_games->setEnabled(false);
    refresh_games->SetSpinning(true);
    if (item_model->IsEmpty()) {
        library_ready = false;
        games_shown = false;
        content_stack->setCurrentWidget(loading_page);
        emit ShowList(true);
    }
    loading_spinner->SetActive(!library_ready);
    // Cached rows remain usable while replacement entries are collected off-screen.
    m_currentView->setEnabled(library_ready);
    tree_view->setRootIndex({});

    switch (game_list_mode) {
    case Settings::GameListMode::TreeView:
        tree_view->UpdateColumnVisibility(item_model);
        break;
    case Settings::GameListMode::GridView:
        grid_view->UpdateIconSize();
        break;
    case Settings::GameListMode::CarouselView:
        carousel_view->UpdateIconSize();
        break;
    }
}

void GameList::PrepareForLaunch() {
    item_model->PrepareForLaunch();
    refresh_games->SetSpinning(false);
    refresh_games->setEnabled(true);
}

void GameList::PopulateAsync(QVector<UISettings::GameDir>& game_dirs) {
    item_model->PopulateAsync(game_dirs);
}

void GameList::SaveInterfaceLayout() {
    tree_view->SaveInterfaceLayout();
}

void GameList::LoadInterfaceLayout() {
    tree_view->LoadInterfaceLayout();
}

QStandardItemModel* GameList::GetModel() const {
    return item_model;
}

void GameList::UnloadController() {
    controller_navigation->UnloadController();
}

void GameList::ResetViewMode() {
    const bool initialized = m_currentView != nullptr;
    const auto mode = UISettings::values.game_list_mode.GetValue();
    if (initialized && game_list_mode == mode) {
        return;
    }
    game_list_mode = mode;

    bool newTreeMode = false;

    switch (mode) {
    case Settings::GameListMode::TreeView:
        m_currentView = tree_view;
        newTreeMode = true;

        break;
    case Settings::GameListMode::GridView:
        m_currentView = grid_view;
        newTreeMode = false;

        break;
    case Settings::GameListMode::CarouselView:
        m_currentView = carousel_view;
        newTreeMode = false;

        break;
    default:
        UNREACHABLE();
    }

    tree_view->setVisible(false);
    grid_view->setVisible(false);
    carousel_view->setVisible(false);

    tree_view->setEnabled(false);
    grid_view->setEnabled(false);
    carousel_view->setEnabled(false);

    // Only the active view may sort the shared model. Restoring an inactive tree
    // header must not reorder the grid after its cached entries have been sorted.
    tree_view->setSortingEnabled(newTreeMode);
    m_currentView->setEnabled(true);
    setFocusProxy(m_currentView);
    content_stack->setCurrentWidget(games_shown ? static_cast<QWidget*>(m_currentView)
                                               : loading_page);
    folder_filter->setVisible(!newTreeMode && folder_filter->count() > 2);
    sort_order->setVisible(!newTreeMode);
    item_model->setSortRole(GameListItem::SortRole);

    auto view = m_currentView->viewport();
    view->installEventFilter(this);

    view->grabGesture(Qt::SwipeGesture);
    view->grabGesture(Qt::PanGesture);

    QScroller::grabGesture(view, QScroller::LeftMouseButtonGesture);

    auto scroller = QScroller::scroller(view);
    QScrollerProperties props;
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(props);

    if (m_isTreeMode != newTreeMode) {
        m_isTreeMode = newTreeMode;
        if (initialized) {
            library_ready = false;
            games_shown = false;
            content_stack->setCurrentWidget(loading_page);
        }
        item_model->SetFlat(!m_isTreeMode);
        if (initialized && !UISettings::values.game_dirs.empty()) {
            item_model->PopulateAsync(UISettings::values.game_dirs);
        }
    }
}

void GameList::OnTextChanged(const QString& new_text) {
    const QString edit_filter_text = new_text.toLower();

    if (m_isTreeMode) {
        tree_view->ApplyFilter(edit_filter_text, item_model);
        empty_results->hide();
    } else {
        auto* view = static_cast<QListView*>(m_currentView);
        const int directory = folder_filter->currentData().isValid()
                                  ? folder_filter->currentData().toInt() : -1;
        int visible = 0;
        for (int row = 0; row < item_model->rowCount(); ++row) {
            const auto* item = item_model->item(row);
            const bool matches = Yuzu::FilterMatches(edit_filter_text, item) &&
                (directory < 0 || item->data(GameListItemPath::SourceDirectoryRole).toInt() == directory);
            view->setRowHidden(row, !matches);
            visible += matches;
        }
        search_field->setFilterResult(visible, item_model->rowCount());
        empty_results->setVisible(visible == 0 && item_model->rowCount() > 0);
    }
}

void GameList::UpdateLibraryFolders() {
    const QSignalBlocker blocker(folder_filter);
    const auto previous = folder_filter->currentData();
    folder_filter->clear();
    folder_filter->addItem(tr("All Games"), -1);
    for (int i = 0; i < UISettings::values.game_dirs.size(); ++i) {
        const auto& dir = UISettings::values.game_dirs[i];
        const auto path = QString::fromStdString(dir.path);
        const bool installed = path == QStringLiteral("SDMC") || path.endsWith(QStringLiteral("NAND"));
        if (installed) {
            bool has_games = false;
            for (int row = 0; row < item_model->rowCount(); ++row) {
                has_games |= item_model->item(row)->data(GameListItemPath::SourceDirectoryRole).toInt() == i;
            }
            if (!has_games || m_isTreeMode) {
                continue;
            }
        }
        auto label = installed ? tr("Installed games (%1)").arg(path) : QDir(path).dirName();
        if (label.isEmpty()) {
            label = path;
        }
        folder_filter->addItem(label, i);
        folder_filter->setItemData(folder_filter->count() - 1, path, Qt::ToolTipRole);
    }
    folder_filter->setCurrentIndex(std::max(0, folder_filter->findData(previous)));
    folder_filter->setVisible(!m_isTreeMode && folder_filter->count() > 2);
}

void GameList::OnPopulatingCompleted(const QStringList& watch_list) {
    refresh_games->SetSpinning(false);
    refresh_games->setEnabled(true);
    library_ready = true;
    loading_spinner->SetActive(false);

    // favorites row
    if (m_isTreeMode) {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                                UISettings::values.favorited_ids.size() == 0);
        tree_view->setExpanded(item_model->invisibleRootItem()->child(0)->index(),
                               UISettings::values.favorites_expanded.GetValue());
    }

    // restore collapsed/expanded flags
    if (m_isTreeMode) {
        const auto* root = item_model->invisibleRootItem();
        for (int i = 1; i < root->rowCount() - 1; ++i) {
            const auto* dir_item = root->child(i);
            const auto type = dir_item->data(GameListItem::TypeRole).value<GameListItemType>();
            if (type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
                type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir) {
                const int dir_index = dir_item->data(GameListDir::GameDirRole).toInt();
                if (dir_index >= 0 && dir_index < UISettings::values.game_dirs.size()) {
                    tree_view->setExpanded(dir_item->index(),
                                           UISettings::values.game_dirs[dir_index].expanded);
                }
            }
        }
    }

    // Watcher updates
    auto* watcher = item_model->GetWatcher();
    auto current_watch_list = watcher->directories();

    constexpr qsizetype LIMIT_WATCH_DIRECTORIES = 5000;
    constexpr int SLICE_SIZE = 25;

    QStringList to_remove, to_add;

    const auto slice = [&](const QStringList& list, std::function<void(const QStringList&)> callback) {
        const int len = (std::min)(list.size(), LIMIT_WATCH_DIRECTORIES);
        for (int i = 0; i < len; i += SLICE_SIZE) {
            auto chunk = list.mid(i, SLICE_SIZE);
            if (!chunk.isEmpty()) {
                callback(chunk);
            }
        }
    };

    // remove any paths not in the new watch list
    for (const auto& path : std::as_const(current_watch_list)) {
        if (!watch_list.contains(path)) {
            to_remove.emplaceBack(path);
        }
    }

    slice(to_remove, [watcher](const QStringList& chunk) { watcher->removePaths(chunk); });

    // add any paths not in the old watch list
    for (const auto& path : std::as_const(watch_list)) {
        if (!current_watch_list.contains(path)) {
            to_add.emplaceBack(path);
        }
    }

    slice(to_add, [watcher](const QStringList& chunk) { watcher->addPaths(chunk); });

    m_currentView->setEnabled(true);
    setFocusProxy(m_currentView);

    UpdateLibraryFolders();
    if (m_isTreeMode) {
        item_model->sort(tree_view->header()->sortIndicatorSection(),
                         tree_view->header()->sortIndicatorOrder());
        // A single source needs no folder level between the player and their games.
        if (item_model->rowCount() == 3 && UISettings::values.favorited_ids.isEmpty()) {
            tree_view->setRootIndex(item_model->index(1, 0));
        }
    } else {
        ApplySort();
    }
    OnTextChanged(search_field->filterText());

    emit PopulatingCompleted();
    emit ShowList(!item_model->IsEmpty());
    AnimateGames();
}

void GameList::ApplySort() {
    if (m_isTreeMode) {
        return;
    }
    if (sort_order->currentIndex() == 0) {
        const auto& recent = UISettings::values.recent_files;
        for (int row = 0; row < item_model->rowCount(); ++row) {
            auto* item = item_model->item(row);
            const auto path = item->data(GameListItemPath::FullPathRole).toString();
            auto rank = recent.indexOf(path);
            if (rank < 0 && QFileInfo(path).isDir()) {
                rank = recent.indexOf(QDir(path).filePath(QStringLiteral("main")));
            }
            item->setData(rank < 0 ? recent.size() : rank, GameListItemPath::RecentOrderRole);
        }
        item_model->setSortRole(GameListItemPath::RecentOrderRole);
    } else {
        item_model->setSortRole(GameListItem::SortRole);
    }
    item_model->sort(GameListModel::COLUMN_NAME,
                     sort_order->currentIndex() == 2 ? Qt::DescendingOrder : Qt::AscendingOrder);
    OnTextChanged(search_field->filterText());
}

void GameList::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!games_shown) {
        setFocusProxy(nullptr);
        QWidget::setFocus(Qt::OtherFocusReason);
    }
    AnimateGames();
}

namespace {
void FadeIn(QWidget* widget, int duration) {
    if (!UIAnimation::IsEnabled(widget)) {
        return;
    }
    auto* opacity = new QGraphicsOpacityEffect(widget);
    opacity->setOpacity(0.0);
    widget->setGraphicsEffect(opacity);
    auto* animation = new QPropertyAnimation(opacity, "opacity", widget);
    animation->setDuration(duration);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(animation, &QPropertyAnimation::finished, widget,
                     [widget] { widget->setGraphicsEffect(nullptr); });
    QTimer::singleShot(0, animation,
                       [animation] { animation->start(QAbstractAnimation::DeleteWhenStopped); });
}
} // namespace

void GameList::FinishWindowEntrance() {
    QTimer::singleShot(UIAnimation::IsEnabled(this) ? 300 : 0, this, [this] {
        window_entrance_complete = true;
        AnimateGames();
    });
}

void GameList::AnimateGames() {
    if (games_shown || !window_entrance_complete || !library_ready || !isVisible() ||
        item_model->IsEmpty()) {
        return;
    }
    // Finish sorting and laying out the cards before making the content page visible.
    ApplySort();
    games_shown = true;
    loading_spinner->SetActive(false);
    FadeIn(m_currentView, 300);
    content_stack->setCurrentWidget(m_currentView);
    UpdateIconSizes();
    m_currentView->doItemsLayout();
    if (!item_model->IsPopulating()) {
        setFocusProxy(m_currentView);
    }
}

void GameList::RefreshGameDirectory() {
    item_model->RefreshGameDirectory();
}

void GameList::RefreshExternalContent() {
    item_model->RefreshExternalContent();
}

void GameList::UpdateIconSizes() {
    switch (game_list_mode) {
    case Settings::GameListMode::GridView:
        grid_view->UpdateIconSize();
        break;
    case Settings::GameListMode::CarouselView:
        carousel_view->UpdateIconSize();
        break;
    case Settings::GameListMode::TreeView:
        break;
    }
}

void GameList::ValidateEntry(const QModelIndex& item) {
    const auto selected = item.sibling(item.row(), 0);

    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game: {
        const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
        if (file_path.isEmpty())
            return;
        const QFileInfo file_info(file_path);
        if (!file_info.exists())
            return;

        if (file_info.isDir()) {
            const QDir dir{file_path};
            const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
            if (matching_main.size() == 1) {
                emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
            }
            return;
        }

        const auto title_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();

        search_field->clear();
        emit GameChosen(file_path, title_id);
        break;
    }
    case GameListItemType::AddDir:
        emit AddDirectory();
        break;
    default:
        break;
    }
}

void GameList::ToggleFavorite(u64 program_id) {
    item_model->ToggleFavorite(program_id);
    if (!m_isTreeMode) {
        m_currentView->viewport()->update();
        return;
    }

    if (UISettings::values.favorited_ids.contains(program_id)) {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                                !search_field->filterText().isEmpty());
        item_model->sort(tree_view->header()->sortIndicatorSection(),
                         tree_view->header()->sortIndicatorOrder());
    } else {
        if (UISettings::values.favorited_ids.size() == 0) {
            tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
        }
    }
}

void GameList::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex item = m_currentView->indexAt(menu_location);
    if (!item.isValid()) {
        if (m_isTreeMode)
            return;

        QMenu blank_menu;
        QAction* addGameDirAction = blank_menu.addAction(tr("&Add New Game Directory"));

        connect(addGameDirAction, &QAction::triggered, this, &GameList::AddDirectory);
        blank_menu.exec(m_currentView->viewport()->mapToGlobal(menu_location));
        return;
    }

    const auto selected = item.sibling(item.row(), 0);
    QMenu context_menu;
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game:
        context_menu.addAction(tr("Choose Cover…"), this, [this, selected] { ChooseCover(selected); });
        context_menu.addSeparator();
        AddGamePopup(context_menu, selected.data(GameListItemPath::ProgramIdRole).toULongLong(),
                     selected.data(GameListItemPath::FullPathRole).toString().toStdString());
        break;
    case GameListItemType::CustomDir:
        AddPermDirPopup(context_menu, selected);
        AddCustomDirPopup(context_menu, selected);
        break;
    case GameListItemType::SdmcDir:
    case GameListItemType::UserNandDir:
    case GameListItemType::SysNandDir:
        AddPermDirPopup(context_menu, selected);
        break;
    case GameListItemType::Favorites:
        AddFavoritesPopup(context_menu);
        break;
    default:
        break;
    }
    context_menu.exec(m_currentView->viewport()->mapToGlobal(menu_location));
}

void GameList::ChooseCover(const QModelIndex& index) {
    const QPersistentModelIndex selected(index);
    const auto source = QFileDialog::getOpenFileName(this, tr("Choose Cover"), {},
                                                    tr("Images (*.png *.jpg *.jpeg *.webp)"));
    if (source.isEmpty() || !selected.isValid()) {
        return;
    }
    QImageReader reader(source);
    reader.setAutoTransform(true);
    if (reader.size().isValid()) {
        reader.setScaledSize(reader.size().scaled(512, 512, Qt::KeepAspectRatio));
    }
    const auto image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Cover Artwork"), tr("This image could not be opened."));
        return;
    }
    const auto path = GameListArtwork::Path(selected.data(GameListItemPath::FullPathRole).toString(),
                                            selected.data(GameListItemPath::ProgramIdRole).toULongLong());
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly) ||
        !image.save(&file, "PNG") || !file.commit()) {
        QMessageBox::warning(this, tr("Cover Artwork"), tr("The cover could not be saved."));
        return;
    }
    item_model->setData(selected, QPixmap::fromImage(image), Qt::DecorationRole);
}

void GameList::AddGamePopup(QMenu& context_menu, u64 program_id, const std::string& path) {
    // TODO(crueter): Refactor this and make it less bad
    QAction* favorite = context_menu.addAction(tr("Favorite"));
    context_menu.addSeparator();
    QAction* start_game = context_menu.addAction(tr("Start Game"));
    QAction* start_game_global =
        context_menu.addAction(tr("Start Game without Custom Configuration"));
    context_menu.addSeparator();
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* open_mod_location = context_menu.addAction(tr("Open Mod Data Location"));
    QAction* open_transferable_shader_cache =
        context_menu.addAction(tr("Open Transferable Pipeline Cache"));
    QAction* ryujinx = context_menu.addAction(tr("Link to Ryujinx"));
    context_menu.addSeparator();
    QMenu* remove_menu = context_menu.addMenu(tr("Remove"));
    QAction* remove_update = remove_menu->addAction(tr("Remove Installed Update"));
    QAction* remove_dlc = remove_menu->addAction(tr("Remove All Installed DLC"));
    QAction* remove_custom_config = remove_menu->addAction(tr("Remove Custom Configuration"));
    QAction* remove_cache_storage = remove_menu->addAction(tr("Remove Cache Storage"));
    QAction* remove_gl_shader_cache = remove_menu->addAction(tr("Remove OpenGL Pipeline Cache"));
    QAction* remove_vk_shader_cache = remove_menu->addAction(tr("Remove Vulkan Pipeline Cache"));
    remove_menu->addSeparator();
    QAction* remove_shader_cache = remove_menu->addAction(tr("Remove All Pipeline Caches"));
    QAction* remove_all_content = remove_menu->addAction(tr("Remove All Installed Contents"));
    QMenu* play_time_menu = context_menu.addMenu(tr("Manage Play Time"));
    QAction* set_play_time = play_time_menu->addAction(tr("Edit Play Time Data"));
    QAction* remove_play_time_data = play_time_menu->addAction(tr("Remove Play Time Data"));
    QMenu* dump_romfs_menu = context_menu.addMenu(tr("Dump RomFS"));
    QAction* dump_romfs = dump_romfs_menu->addAction(tr("Dump RomFS"));
    QAction* dump_romfs_sdmc = dump_romfs_menu->addAction(tr("Dump RomFS to SDMC"));
    QAction* verify_integrity = context_menu.addAction(tr("Verify Integrity"));
    QAction* copy_tid = context_menu.addAction(tr("Copy Title ID to Clipboard"));
#if !defined(__APPLE__)
    QMenu* shortcut_menu = context_menu.addMenu(tr("Create Shortcut"));
    QAction* create_desktop_shortcut = shortcut_menu->addAction(tr("Add to Desktop"));
    QAction* create_applications_menu_shortcut =
        shortcut_menu->addAction(tr("Add to Applications Menu"));
#endif
    context_menu.addSeparator();
    QAction* properties = context_menu.addAction(tr("Configure Game"));

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));
    open_save_location->setVisible(program_id != 0);
    open_mod_location->setVisible(program_id != 0);
    open_transferable_shader_cache->setVisible(program_id != 0);
    remove_update->setVisible(program_id != 0);
    remove_dlc->setVisible(program_id != 0);
    remove_gl_shader_cache->setVisible(program_id != 0);
    remove_vk_shader_cache->setVisible(program_id != 0);
    remove_shader_cache->setVisible(program_id != 0);
    remove_all_content->setVisible(program_id != 0);

    connect(favorite, &QAction::triggered, this,
            [this, program_id]() { ToggleFavorite(program_id); });
    connect(open_save_location, &QAction::triggered, this, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path);
    });
    connect(start_game, &QAction::triggered, this,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Normal); });
    connect(start_game_global, &QAction::triggered, this,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Global); });
    connect(open_mod_location, &QAction::triggered, this, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path);
    });
    connect(open_transferable_shader_cache, &QAction::triggered, this,
            [this, program_id]() { emit OpenTransferableShaderCacheRequested(program_id); });
    connect(remove_all_content, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, QtCommon::Game::InstalledEntryType::Game);
    });
    connect(remove_update, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, QtCommon::Game::InstalledEntryType::Update);
    });
    connect(remove_dlc, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id,
                                           QtCommon::Game::InstalledEntryType::AddOnContent);
    });
    connect(remove_gl_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::GlShaderCache,
                                 path);
    });
    connect(remove_vk_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::VkShaderCache,
                                 path);
    });
    connect(remove_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::AllShaderCache,
                                 path);
    });
    connect(remove_custom_config, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id,
                                 QtCommon::Game::GameListRemoveTarget::CustomConfiguration, path);
    });
    connect(set_play_time, &QAction::triggered, this,
            [this, program_id]() { emit SetPlayTimeRequested(program_id); });
    connect(remove_play_time_data, &QAction::triggered, this,
            [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, this, [this, program_id, path] {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::CacheStorage,
                                 path);
    });
    connect(dump_romfs, &QAction::triggered, this, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::Normal);
    });
    connect(dump_romfs_sdmc, &QAction::triggered, this, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::SDMC);
    });
    connect(verify_integrity, &QAction::triggered, this,
            [this, path]() { emit VerifyIntegrityRequested(path); });
    connect(copy_tid, &QAction::triggered, this,
            [this, program_id]() { emit CopyTIDRequested(program_id); });
#if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, this, [this, program_id, path]() {
        emit CreateShortcut(program_id, path, QtCommon::Game::ShortcutTarget::Desktop);
    });
    connect(create_applications_menu_shortcut, &QAction::triggered, this,
            [this, program_id, path]() {
                emit CreateShortcut(program_id, path, QtCommon::Game::ShortcutTarget::Applications);
            });
#endif
    connect(properties, &QAction::triggered, this,
            [this, path]() { emit OpenPerGameGeneralRequested(path); });

    connect(ryujinx, &QAction::triggered, this,
            [this, program_id]() { emit LinkToRyujinxRequested(program_id); });
};

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir =
        UISettings::values.game_dirs[selected.data(GameListDir::GameDirRole).toInt()];

    QAction* deep_scan = context_menu.addAction(tr("Scan Subfolders"));
    QAction* delete_dir = context_menu.addAction(tr("Remove Game Directory"));

    deep_scan->setCheckable(true);
    deep_scan->setChecked(game_dir.deep_scan);

    connect(deep_scan, &QAction::triggered, this, [this, &game_dir] {
        game_dir.deep_scan = !game_dir.deep_scan;
        PopulateAsync(UISettings::values.game_dirs);
    });
    connect(delete_dir, &QAction::triggered, this, [this, &game_dir, selected] {
        UISettings::values.game_dirs.removeOne(game_dir);
        item_model->invisibleRootItem()->removeRow(selected.row());
        OnTextChanged(search_field->filterText());
    });
}

void GameList::AddPermDirPopup(QMenu& context_menu, QModelIndex selected) {
    const int game_dir_index = selected.data(GameListDir::GameDirRole).toInt();

    QAction* move_up = context_menu.addAction(tr("\u25B2 Move Up"));
    QAction* move_down = context_menu.addAction(tr("\u25bc Move Down"));
    QAction* open_directory_location = context_menu.addAction(tr("Open Directory Location"));

    const int row = selected.row();

    move_up->setEnabled(row > 1);
    move_down->setEnabled(row < item_model->rowCount() - 2);

    connect(move_up, &QAction::triggered, this, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row - 1, 0).data(GameListDir::GameDirRole).toInt();
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row - 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        QList<QStandardItem*> items = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row - 1, items);
        tree_view->setExpanded(selected.sibling(row - 1, 0),
                               UISettings::values.game_dirs[other_index].expanded);
    });

    connect(move_down, &QAction::triggered, this, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row + 1, 0).data(GameListDir::GameDirRole).toInt();
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row + 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        const QList<QStandardItem*> items = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row + 1, items);
        tree_view->setExpanded(selected.sibling(row + 1, 0),
                               UISettings::values.game_dirs[other_index].expanded);
    });

    connect(open_directory_location, &QAction::triggered, this, [this, game_dir_index] {
        emit OpenDirectory(
            QString::fromStdString(UISettings::values.game_dirs[game_dir_index].path));
    });
}

void GameList::AddFavoritesPopup(QMenu& context_menu) {
    QAction* clear = context_menu.addAction(tr("Clear"));

    connect(clear, &QAction::triggered, this, [this] {
        UISettings::values.favorited_ids.clear();
        item_model->invisibleRootItem()->child(0)->removeRows(
            0, item_model->invisibleRootItem()->child(0)->rowCount());
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
    });
}

void GameList::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void GameList::RetranslateUI() {
    item_model->RetranslateUI();
    add_games->setToolTip(tr("Add games"));
    add_games->setAccessibleName(tr("Add games"));
    sort_order->setAccessibleName(tr("Sort games"));
    sort_order->setItemText(0, tr("Recently played"));
    sort_order->setItemText(1, tr("Name A–Z"));
    sort_order->setItemText(2, tr("Name Z–A"));
    empty_results->setText(tr("No games match your search."));
    folder_filter->setAccessibleName(tr("Game folders"));
    UpdateLibraryFolders();
}

void GameList::SetupScrollAnimation() {
    auto setup = [this](QVariantAnimation* anim, QScrollBar* bar) {
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setDuration(200);
        connect(anim, &QVariantAnimation::valueChanged, this,
                [bar](const QVariant& value) { bar->setValue(value.toInt()); });
    };

    vertical_scroll = new QVariantAnimation(this);
    horizontal_scroll = new QVariantAnimation(this);

    setup(vertical_scroll, tree_view->verticalScrollBar());
    setup(horizontal_scroll, tree_view->horizontalScrollBar());

    setup(vertical_scroll, grid_view->verticalScrollBar());
    setup(horizontal_scroll, grid_view->horizontalScrollBar());
}

bool GameList::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_currentView->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);

        bool horizontal = wheelEvent->modifiers() & Qt::ShiftModifier;

        int deltaX = wheelEvent->angleDelta().x();
        int deltaY = wheelEvent->angleDelta().y();

        if (horizontal && deltaY != 0 && deltaX == 0) {
            deltaX = deltaY;
            deltaY = 0;
        }

        if (deltaY != 0) {
            if (vertical_scroll->state() == QAbstractAnimation::Stopped)
                vertical_scroll_target = m_currentView->verticalScrollBar()->value();

            vertical_scroll_target -= deltaY;
            vertical_scroll_target =
                qBound(0, vertical_scroll_target, m_currentView->verticalScrollBar()->maximum());

            vertical_scroll->stop();
            vertical_scroll->setStartValue(m_currentView->verticalScrollBar()->value());
            vertical_scroll->setEndValue(vertical_scroll_target);
            vertical_scroll->start();
        }

        if (deltaX != 0) {
            if (horizontal_scroll->state() == QAbstractAnimation::Stopped)
                horizontal_scroll_target = m_currentView->horizontalScrollBar()->value();

            horizontal_scroll_target -= deltaX;
            horizontal_scroll_target = qBound(0, horizontal_scroll_target,
                                              m_currentView->horizontalScrollBar()->maximum());

            horizontal_scroll->stop();
            horizontal_scroll->setStartValue(m_currentView->horizontalScrollBar()->value());
            horizontal_scroll->setEndValue(horizontal_scroll_target);
            horizontal_scroll->start();
        }

        return true;
    }

    if (obj == m_currentView->viewport() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        QModelIndex index = m_currentView->indexAt(mouseEvent->pos());
        if (!index.isValid()) {
            m_currentView->selectionModel()->clearSelection();
            m_currentView->setCurrentIndex(QModelIndex());
        }
    }

    if (obj == grid_view->viewport() && event->type() == QEvent::Resize) {
        grid_view->UpdateIconSize();
        return true;
    }

    if (obj == carousel_view->viewport() && event->type() == QEvent::Resize) {
        carousel_view->UpdateIconSize();
        return true;
    }

    return QWidget::eventFilter(obj, event);
}

GameListPlaceholder::GameListPlaceholder(MainWindow* parent) : QWidget{parent} {
    layout = new QVBoxLayout;
    image = new QLabel;
    text = new QLabel;
    layout->setAlignment(Qt::AlignCenter);
    image->setPixmap(QIcon::fromTheme(QStringLiteral("plus_folder")).pixmap(200));

    RetranslateUI();
    QFont font = text->font();
    font.setPointSize(20);
    text->setFont(font);
    text->setAlignment(Qt::AlignHCenter);
    image->setAlignment(Qt::AlignHCenter);

    layout->addWidget(image);
    layout->addWidget(text);
    setLayout(layout);
}

GameListPlaceholder::~GameListPlaceholder() = default;

void GameListPlaceholder::mouseDoubleClickEvent(QMouseEvent* event) {
    emit GameListPlaceholder::AddDirectory();
}

void GameListPlaceholder::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void GameListPlaceholder::RetranslateUI() {
    text->setText(tr("Double-click to add a new folder to the game list"));
}
