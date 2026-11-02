// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThreadPool>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/game_list/worker.h"

namespace {
struct LooseDumpVersion {
    QString game_directory;
    u64 version;
};

std::optional<LooseDumpVersion> GetLooseDumpVersion(const QStandardItem* item) {
    if (item->data(GameListItemPath::FileTypeRole).toString() != QStringLiteral("Directory") ||
        item->data(GameListItemPath::ProgramIdRole).toULongLong() == 0) {
        return std::nullopt;
    }
    QDir exefs(item->data(GameListItemPath::FullPathRole).toString());
    if (exefs.dirName() != QStringLiteral("exefs") ||
        !QFileInfo(exefs.filePath(QStringLiteral("main"))).isFile()) {
        return std::nullopt;
    }
    QDir content = exefs;
    if (!content.cdUp()) {
        return std::nullopt;
    }
    // Only consolidate self-contained dumps. An update with no RomFS may be a patch
    // requiring its base, and must not replace a launchable entry in the library.
    if (!QFileInfo(content.filePath(QStringLiteral("romfs"))).isDir() &&
        exefs.entryList({QStringLiteral("*.romfs")}, QDir::Files).isEmpty()) {
        return std::nullopt;
    }
    if (content.dirName() == QStringLiteral("base")) {
        content.cdUp();
        return LooseDumpVersion{content.canonicalPath(), 0};
    }
    bool numeric = false;
    const auto version = content.dirName().toULongLong(&numeric);
    if (!numeric || version == 0 || !content.cdUp() ||
        content.dirName() != QStringLiteral("updates") || !content.cdUp()) {
        return std::nullopt;
    }
    return LooseDumpVersion{content.canonicalPath(), version};
}

void AppendLibraryEntry(QStandardItem* parent, const QList<QStandardItem*>& row) {
    const auto* name = row.front();
    if (const auto candidate = GetLooseDumpVersion(name)) {
        for (int index = 0; index < parent->rowCount(); ++index) {
            const auto* existing = parent->child(index);
            if (existing->data(GameListItemPath::ProgramIdRole) !=
                    name->data(GameListItemPath::ProgramIdRole) ||
                existing->data(GameListItemPath::SourceDirectoryRole) !=
                    name->data(GameListItemPath::SourceDirectoryRole)) {
                continue;
            }
            const auto previous = GetLooseDumpVersion(existing);
            if (!previous || previous->game_directory != candidate->game_directory) {
                continue;
            }
            if (candidate->version <= previous->version) {
                qDeleteAll(row);
                return;
            }
            parent->removeRow(index);
            parent->insertRow(index, row);
            return;
        }
    }
    parent->appendRow(row);
}
} // namespace

GameListModel::GameListModel(std::shared_ptr<FileSys::VfsFilesystem> vfs_,
                             FileSys::ManualContentProvider* provider_,
                             const PlayTime::PlayTimeManager& play_time_manager_,
                             Core::System& system_, QObject* parent)
    : QStandardItemModel{parent}, vfs{std::move(vfs_)}, provider{provider_},
      play_time_manager{play_time_manager_}, system{system_} {
    watcher = new QFileSystemWatcher(this);
    external_watcher = new QFileSystemWatcher(this);

    connect(watcher, &QFileSystemWatcher::directoryChanged, this,
            &GameListModel::RefreshGameDirectory);
    connect(external_watcher, &QFileSystemWatcher::directoryChanged, this,
            &GameListModel::RefreshExternalContent);

    ResetExternalWatcher();

    insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    setSortRole(GameListItemPath::SortRole);
}

GameListModel::~GameListModel() {
    StopWorker();
}

void GameListModel::PopulateAsync(QVector<UISettings::GameDir>& game_dirs) {
    StopWorker();
    populating = true;
    snapshot_directories = game_dirs;
    pending_entries.clear();
    flat_directories.clear();
    if (!m_flat) {
        removeRows(0, rowCount());
    }
    emit PopulatingStarted();
    RestoreSnapshot(game_dirs);

    current_worker = std::make_unique<GameListWorker>(vfs, provider, game_dirs,
                                                      play_time_manager, system);
    auto* worker = current_worker.get();
    connect(
        worker, &GameListWorker::DataAvailable, this,
        [this, worker] {
            // A queued notification from a stopped scan must not drain its replacement.
            if (current_worker.get() == worker) {
                worker->ProcessEvents(this);
            }
        },
        Qt::QueuedConnection);

    QThreadPool::globalInstance()->start(worker);
}

void GameListModel::AddDirEntry(GameListDir* entry_items) {
    if (m_flat) {
        flat_directories.emplace_back(entry_items);
        return;
    }
    invisibleRootItem()->appendRow(entry_items);
}

void GameListModel::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    entry_items.front()->setData(parent->data(GameListDir::GameDirRole),
                                 GameListItemPath::SourceDirectoryRole);
    if (m_flat) {
        AppendLibraryEntry(pending_entries.invisibleRootItem(), entry_items);
    } else {
        AppendLibraryEntry(parent, entry_items);
    }
}

void GameListModel::DonePopulating(const QStringList& watch_list) {
    if (m_flat) {
        ReconcileEntries();
        SaveSnapshot();
    }
    populating = false;
    emit ShowList(!IsEmpty());

    if (!m_flat) {
        invisibleRootItem()->appendRow(new GameListAddDir());
        invisibleRootItem()->insertRow(0, new GameListFavorites());

        for (const auto id : std::as_const(UISettings::values.favorited_ids)) {
            AddFavorite(id);
        }
    }

    emit PopulatingCompleted(watch_list);
}

bool GameListModel::IsEmpty() const {
    for (int i = 0; i < rowCount(); i++) {
        const QStandardItem* child = invisibleRootItem()->child(i);
        const auto type = static_cast<GameListItemType>(child->type());

        if (!child->hasChildren() &&
            (type == GameListItemType::SdmcDir || type == GameListItemType::UserNandDir ||
             type == GameListItemType::SysNandDir)) {
            invisibleRootItem()->removeRow(child->row());
            i--;
        }
    }

    return !invisibleRootItem()->hasChildren();
}

void GameListModel::ToggleFavorite(u64 program_id) {
    if (!UISettings::values.favorited_ids.contains(program_id)) {
        UISettings::values.favorited_ids.append(program_id);
        AddFavorite(program_id);
    } else {
        UISettings::values.favorited_ids.removeOne(program_id);
        RemoveFavorite(program_id);
    }
    emit SaveConfig();
}

void GameListModel::AddFavorite(u64 program_id) {
    if (m_flat) {
        return;
    }
    auto* favorites_row = item(0);

    for (int i = 1; i < rowCount() - 1; i++) {
        const auto* folder = item(i);
        for (int j = 0; j < folder->rowCount(); j++) {
            if (folder->child(j)->data(GameListItemPath::ProgramIdRole).toULongLong() ==
                program_id) {
                QList<QStandardItem*> list;
                for (int k = 0; k < COLUMN_COUNT; k++) {
                    list.append(folder->child(j, k)->clone());
                }
                list[0]->setData(folder->child(j)->data(GameListItem::SortRole),
                                 GameListItem::SortRole);
                list[0]->setText(folder->child(j)->data(Qt::DisplayRole).toString());

                favorites_row->appendRow(list);
                return;
            }
        }
    }
}

void GameListModel::RemoveFavorite(u64 program_id) {
    if (m_flat) {
        return;
    }
    auto* favorites_row = item(0);

    for (int i = 0; i < favorites_row->rowCount(); i++) {
        const auto* game = favorites_row->child(i);
        if (game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
            favorites_row->removeRow(i);
            return;
        }
    }
}

void GameListModel::PrepareForLaunch() {
    if (current_worker) {
        current_worker->StopListing();
        StopWorker();
    }
    pending_entries.clear();
    populating = false;
}

void GameListModel::StopWorker() {
    if (!current_worker) {
        return;
    }

    // The worker may still be inside ProcessEvents further up the stack (a nested event loop
    // can trigger a repopulate), so stop it now but defer freeing it to the event loop.
    current_worker->disconnect();
    current_worker->Stop();
    current_worker.release()->deleteLater();
}

void GameListModel::Repopulate() {
    // CreateFactories replaces the content providers a running game reads from. Stopping
    // emulation repopulates the list anyway, so skip watcher-driven reloads until then.
    if (QtCommon::system->IsPoweredOn()) {
        LOG_INFO(Frontend, "Emulation is running, deferring game list reload.");
        return;
    }

    StopWorker();
    QtCommon::system->GetFileSystemController().CreateFactories(*QtCommon::vfs);
    PopulateAsync(UISettings::values.game_dirs);
}

void GameListModel::RefreshGameDirectory() {
    ResetExternalWatcher();
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        Repopulate();
    }
}

void GameListModel::RefreshExternalContent() {
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "External content directory changed. Clearing metadata cache.");
        QtCommon::Game::ResetMetadata(false);
        Repopulate();
    }
}

void GameListModel::ResetExternalWatcher() {
    auto watch_dirs = external_watcher->directories();
    if (!watch_dirs.isEmpty()) {
        external_watcher->removePaths(watch_dirs);
    }

    for (const std::string& dir : Settings::values.external_content_dirs) {
        external_watcher->addPath(QString::fromStdString(dir));
    }
}

void GameListModel::RetranslateUI() {
    setHeaderData(COLUMN_NAME, Qt::Horizontal, tr("Name"));
    setHeaderData(COLUMN_ADD_ONS, Qt::Horizontal, tr("Add-ons"));
    setHeaderData(COLUMN_FILE_TYPE, Qt::Horizontal, tr("File type"));
    setHeaderData(COLUMN_SIZE, Qt::Horizontal, tr("Size"));
    setHeaderData(COLUMN_PLAY_TIME, Qt::Horizontal, tr("Play time"));
}

QFileSystemWatcher* GameListModel::GetWatcher() const {
    return watcher;
}

void GameListModel::SetFlat(bool flat) {
    if (m_flat != flat) {
        StopWorker();
        removeRows(0, rowCount());
    }
    m_flat = flat;
}

namespace {
constexpr qint64 MaxSnapshotBytes = 64 * 1024 * 1024;
constexpr int SnapshotVersion = 1;
constexpr int MaxSnapshotEntries = 4096;

QString SnapshotPath() {
    return QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list" / "library.json"));
}

QJsonArray SnapshotSources(const QVector<UISettings::GameDir>& directories) {
    QJsonArray sources;
    for (const auto& directory : directories) {
        sources.append(QJsonObject{{QStringLiteral("path"), QString::fromStdString(directory.path)},
                                   {QStringLiteral("deep_scan"), directory.deep_scan}});
    }
    return sources;
}

QJsonObject SourceStamp(const QString& path) {
    const QFileInfo source(path);
    const QFileInfo file(source.isDir() ? QDir(path).filePath(QStringLiteral("main")) : path);
    if (!file.isFile()) {
        return {};
    }
    return {{QStringLiteral("size"), QString::number(file.size())},
            {QStringLiteral("modified"), QString::number(file.lastModified().toMSecsSinceEpoch())}};
}

QString EntryKey(const QStandardItem* item) {
    return item->data(GameListItemPath::FullPathRole).toString() + QChar(0) +
           item->data(GameListItemPath::ProgramIdRole).toString() + QChar(0) +
           item->data(GameListItemPath::SourceDirectoryRole).toString();
}
} // namespace

void GameListModel::RestoreSnapshot(const QVector<UISettings::GameDir>& game_dirs) {
    if (!m_flat || snapshot_attempted || !UISettings::values.cache_game_list) {
        return;
    }
    snapshot_attempted = true;
    snapshot_directories = game_dirs;
    QFile file(SnapshotPath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxSnapshotBytes) {
        return;
    }
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    const auto entries = document.value(QStringLiteral("entries")).toArray();
    if (document.value(QStringLiteral("version")).toInt() != SnapshotVersion ||
        document.value(QStringLiteral("sources")).toArray() != SnapshotSources(game_dirs) ||
        entries.size() > MaxSnapshotEntries) {
        return;
    }
    for (const auto& value : entries) {
        const auto entry = value.toObject();
        const auto path = entry.value(QStringLiteral("path")).toString();
        const int directory = entry.value(QStringLiteral("directory")).toInt(-1);
        const auto stamp = SourceStamp(path);
        if (directory < 0 || directory >= game_dirs.size() || stamp.isEmpty() ||
            stamp != entry.value(QStringLiteral("stamp")).toObject()) {
            continue;
        }
        const u64 id = entry.value(QStringLiteral("id")).toString().toULongLong(nullptr, 16);
        const auto title = entry.value(QStringLiteral("title")).toString();
        const auto type = entry.value(QStringLiteral("type")).toString();
        const auto patches = entry.value(QStringLiteral("patches")).toString();
        const auto picture =
            QByteArray::fromBase64(entry.value(QStringLiteral("icon")).toString().toLatin1());
        const std::vector<u8> icon(picture.begin(), picture.end());
        const u64 play_time = play_time_manager.GetPlayTime(id);
        auto* name = new GameListItemPath(path, icon, title, type, id, play_time, patches);
        name->setData(directory, GameListItemPath::SourceDirectoryRole);
        AppendLibraryEntry(invisibleRootItem(),
            {name, new GameListItem(type),
             new GameListItemSize(entry.value(QStringLiteral("size")).toString().toULongLong()),
             new GameListItemPlayTime(play_time), new GameListItem(patches)});
    }
    if (rowCount() > 0) {
        LOG_INFO(Frontend, "Restored {} cached library entries; refreshing in the background",
                 rowCount());
        emit ShowList(true);
        emit SnapshotAvailable();
    }
}

void GameListModel::SaveSnapshot() {
    if (!UISettings::values.cache_game_list || rowCount() > MaxSnapshotEntries) {
        return;
    }
    QJsonArray entries;
    for (int row = 0; row < rowCount(); ++row) {
        const auto* name = item(row);
        const auto path = name->data(GameListItemPath::FullPathRole).toString();
        const auto stamp = SourceStamp(path);
        if (stamp.isEmpty()) {
            continue;
        }
        QByteArray icon;
        QBuffer buffer(&icon);
        buffer.open(QIODevice::WriteOnly);
        const auto picture = name->data(Qt::DecorationRole).value<QPixmap>();
        if (!picture.isNull()) {
            picture.save(&buffer, "PNG");
        }
        entries.append(QJsonObject{
            {QStringLiteral("path"), path},
            {QStringLiteral("stamp"), stamp},
            {QStringLiteral("directory"),
             name->data(GameListItemPath::SourceDirectoryRole).toInt()},
            {QStringLiteral("id"),
             QString::number(name->data(GameListItemPath::ProgramIdRole).toULongLong(), 16)},
            {QStringLiteral("title"), name->data(GameListItemPath::TitleRole).toString()},
            {QStringLiteral("type"), name->data(GameListItemPath::FileTypeRole).toString()},
            {QStringLiteral("size"),
             item(row, COLUMN_SIZE)->data(GameListItemSize::SizeRole).toString()},
            {QStringLiteral("patches"), item(row, COLUMN_ADD_ONS)->text()},
            {QStringLiteral("icon"), QString::fromLatin1(icon.toBase64())}});
    }
    const QJsonDocument document(
        QJsonObject{{QStringLiteral("version"), SnapshotVersion},
                    {QStringLiteral("sources"), SnapshotSources(snapshot_directories)},
                    {QStringLiteral("entries"), entries}});
    const auto bytes = document.toJson(QJsonDocument::Compact);
    const auto path = SnapshotPath();
    if (bytes.size() > MaxSnapshotBytes || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        LOG_WARNING(Frontend, "Could not save the library display cache");
    }
}

void GameListModel::ReconcileEntries() {
    // Preserve model indexes, selection, and scroll position for games that still exist.
    // The worker owns the fresh snapshot until it is complete; partial scans never blank
    // the visible library or publish an incomplete cache.
    QHash<QString, QStandardItem*> incoming;
    for (int row = 0; row < pending_entries.rowCount(); ++row) {
        incoming.insert(EntryKey(pending_entries.item(row)), pending_entries.item(row));
    }
    for (int row = rowCount() - 1; row >= 0; --row) {
        auto* fresh = incoming.take(EntryKey(item(row)));
        if (!fresh) {
            removeRow(row);
            continue;
        }
        const int fresh_row = fresh->row();
        for (int column = 0; column < COLUMN_COUNT; ++column) {
            auto* existing = item(row, column);
            auto* replacement = pending_entries.item(fresh_row, column);
            if (column != COLUMN_NAME) {
                if (itemData(index(row, column)) !=
                    pending_entries.itemData(pending_entries.index(fresh_row, column))) {
                    setItem(row, column, pending_entries.takeItem(fresh_row, column));
                }
                continue;
            }
            const auto roles = pending_entries.itemData(replacement->index());
            for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
                if (role.key() == Qt::DecorationRole) {
                    if (existing->data(role.key()).value<QPixmap>().toImage() ==
                        role.value().value<QPixmap>().toImage()) {
                        continue;
                    }
                } else if (existing->data(role.key()) == role.value()) {
                    continue;
                }
                existing->setData(role.value(), role.key());
            }
        }
    }
    while (pending_entries.rowCount() > 0) {
        const auto row = pending_entries.takeRow(0);
        if (incoming.contains(EntryKey(row.front()))) {
            appendRow(row);
        } else {
            qDeleteAll(row);
        }
    }
}
