// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises library widgets and display caching without booting a game.
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/game_list.h"
#include "yuzu/game/game_grid.h"
#include "yuzu/game/game_tree.h"
#include "yuzu/game/search_field.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QSignalSpy>
#include <QGraphicsOpacityEffect>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QPushButton>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(test)                                                          \
  do {                                                                         \
    if (!(test)) {                                                             \
      std::fprintf(stderr, "%s failed at %d\n", #test, __LINE__);              \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

class CountingDirectory final : public FileSys::VectorVfsDirectory {
public:
  using VectorVfsDirectory::VectorVfsDirectory;
  std::vector<FileSys::VirtualFile> GetFiles() const override {
    ++enumerations;
    return VectorVfsDirectory::GetFiles();
  }
  mutable int enumerations = 0;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  REQUIRE(temporary.isValid());
  const auto root = temporary.path();
  for (const auto path :
       {Common::FS::EdenPath::CacheDir, Common::FS::EdenPath::PlayTimeDir,
        Common::FS::EdenPath::LogDir}) {
    Common::FS::SetEdenPath(path, root.toStdString());
  }
  Common::Log::Initialize();
  Core::System system;
  PlayTime::PlayTimeManager play_time;
  auto filesystem = std::make_shared<FileSys::RealVfsFilesystem>();
  FileSys::ManualContentProvider provider;
  {
    auto content = std::make_shared<FileSys::VectorVfsDirectory>();
    auto exefs = std::make_shared<FileSys::VectorVfsDirectory>(
        std::vector<FileSys::VirtualFile>{}, std::vector<FileSys::VirtualDir>{}, "exefs", content);
    auto main = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{}, "main", exefs);
    exefs->AddFile(main);
    auto romfs = std::make_shared<CountingDirectory>(
        std::vector<FileSys::VirtualFile>{}, std::vector<FileSys::VirtualDir>{}, "romfs");
    romfs->AddFile(std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>{1, 2, 3}, "asset"));
    content->AddDirectory(romfs);
    for (const bool directory_constructor : {false, true}) {
      romfs->enumerations = 0;
      std::unique_ptr<Loader::AppLoader_DeconstructedRomDirectory> loader;
      if (directory_constructor) {
        loader = std::make_unique<Loader::AppLoader_DeconstructedRomDirectory>(exefs);
      } else {
        loader = std::make_unique<Loader::AppLoader_DeconstructedRomDirectory>(main);
      }
      REQUIRE(romfs->enumerations == 0);
      FileSys::VirtualFile first, second;
      REQUIRE(loader->ReadRomFS(first) == Loader::ResultStatus::Success);
      REQUIRE(first && romfs->enumerations > 0);
      const int reads = romfs->enumerations;
      REQUIRE(loader->ReadRomFS(second) == Loader::ResultStatus::Success);
      REQUIRE(first == second && reads == romfs->enumerations);
    }
  }
  UISettings::values.game_list_mode = Settings::GameListMode::GridView;
  UISettings::values.game_icon_size = 256;
  UISettings::values.cache_game_list = true;
  UISettings::values.game_dirs.clear();
  UISettings::GameDir directory;
  directory.path = root.toStdString();
  UISettings::values.game_dirs.push_back(directory);
  const auto path = root + QStringLiteral("/test.nsp");
  QFile source(path);
  REQUIRE(source.open(QIODevice::WriteOnly));
  source.write("fixture");
  source.close();
  GameListDir parent(UISettings::values.game_dirs[0]);
  const auto entry = [&](QString title) {
    return QList<QStandardItem *>{
        new GameListItemPath(path, {}, title, QStringLiteral("NSP"), 1, 0, {}),
        new GameListItem(QStringLiteral("NSP")), new GameListItemSize(7),
        new GameListItemPlayTime(0), new GameListItem(QString{})};
  };
  const auto cache = root + QStringLiteral("/game_list/library.json");
  {
    GameListModel model({}, nullptr, play_time, system);
    model.SetFlat(true);
    model.RestoreSnapshot(UISettings::values.game_dirs);
    model.AddEntry(entry(QStringLiteral("Original")), &parent);
    REQUIRE(model.rowCount() == 0); // A partial scan is not published.
    model.DonePopulating({});
    REQUIRE(model.rowCount() == 1 && QFile::exists(cache));
    const QPersistentModelIndex selected(model.index(0, 0));
    model.AddEntry(entry(QStringLiteral("Renamed")), &parent);
    REQUIRE(selected.data().toString() == QStringLiteral("Original"));
    model.DonePopulating({});
    REQUIRE(selected.isValid() &&
            selected.data().toString() == QStringLiteral("Renamed"));
    REQUIRE(model.rowCount() == 1);
  }
  {
    GameListModel restored({}, nullptr, play_time, system);
    restored.SetFlat(true);
    restored.RestoreSnapshot(UISettings::values.game_dirs);
    REQUIRE(restored.rowCount() == 1);
    REQUIRE(restored.item(0)->data(GameListItemPath::TitleRole) ==
            QStringLiteral("Renamed"));
  }
  const QString second_path = root + QStringLiteral("/second.nsp");
  QFile second_source(second_path);
  REQUIRE(second_source.open(QIODevice::WriteOnly));
  second_source.write("second fixture");
  second_source.close();
  const auto second_entry = [&] {
    auto row = entry(QStringLiteral("Zulu"));
    row.front()->setData(second_path, GameListItemPath::FullPathRole);
    row.front()->setData(qulonglong(2), GameListItemPath::ProgramIdRole);
    return row;
  };
  UISettings::values.recent_files = QStringList{second_path};
  {
    GameListModel model({}, nullptr, play_time, system);
    model.SetFlat(true);
    model.RestoreSnapshot(UISettings::values.game_dirs);
    model.AddEntry(entry(QStringLiteral("Renamed")), &parent);
    model.AddEntry(second_entry(), &parent);
    model.DonePopulating({});
  }
  {
    GameList library(filesystem, &provider, play_time, system);
    library.resize(900, 650);
    auto *search = library.findChild<GameListSearchField *>();
    auto *button =
        library.findChild<QToolButton *>(QStringLiteral("LibrarySearchButton"));
    auto *editor = search->findChild<QLineEdit *>();
    auto *header =
        library.findChild<QWidget *>(QStringLiteral("GameLibraryHeader"));
    auto* grid = library.findChild<GameGrid*>();
    auto* tree = library.findChild<GameTree*>();
    auto* spinner = library.findChild<QWidget*>(QStringLiteral("LibraryLoadingSpinner"));
    auto* model = qobject_cast<GameListModel*>(library.GetModel());
    REQUIRE(search && button && editor && header && grid && tree && spinner && model);
    REQUIRE(model->item(0)->data(GameListItemPath::TitleRole) == QStringLiteral("Zulu"));
    library.show();
    REQUIRE(search->width() == 40 && editor->isHidden());
    REQUIRE(!editor->hasFocus() && !button->hasFocus());
    REQUIRE(grid->isHidden() && spinner->isHidden()); // Cached, but the window is still fading.
    QTest::qWait(100);
    REQUIRE(grid->isHidden());
    library.FinishWindowEntrance();
    QTest::qWait(250);
    REQUIRE(grid->isHidden() && spinner->isHidden());
    QTest::qWait(150);
    REQUIRE(grid->isVisible() && spinner->isHidden());
    auto* fade = qobject_cast<QGraphicsOpacityEffect*>(grid->graphicsEffect());
    REQUIRE(fade && fade->opacity() > 0 && fade->opacity() < 1);
    QTest::qWait(300);
    REQUIRE(grid->graphicsEffect() == nullptr);
    REQUIRE(!tree->isSortingEnabled());
    tree->header()->setSortIndicator(0, Qt::DescendingOrder);
    library.LoadInterfaceLayout();
    REQUIRE(model->item(0)->data(GameListItemPath::TitleRole) == QStringLiteral("Zulu"));
    const auto first_position = grid->visualRect(model->index(0, 0));
    library.OnPopulate();
    REQUIRE(grid->isVisible() && spinner->isHidden() && grid->isEnabled());
    auto* add = library.findChild<QPushButton*>(QStringLiteral("LibraryAdd"));
    auto* settings = library.findChild<QPushButton*>(QStringLiteral("LibrarySettings"));
    auto* refresh = library.findChild<QPushButton*>(QStringLiteral("LibraryRefresh"));
    REQUIRE(add && settings && refresh);
    REQUIRE(add->mapTo(&library, QPoint{}).y() > library.height() - 100);
    REQUIRE(add->mapTo(&library, QPoint{}).x() > library.width() - 100);
    REQUIRE(settings->parentWidget() == header && refresh->parentWidget() == header);
    REQUIRE(settings->geometry().right() < refresh->geometry().left());
    library.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-library.png"));
    QSignalSpy settings_requested(&library, &GameList::SettingsRequested);
    QTest::mouseClick(settings, Qt::LeftButton);
    REQUIRE(settings_requested.count() == 1);
    QVector<UISettings::GameDir> empty_directories;
    model->PopulateAsync(empty_directories);
    REQUIRE(model->IsPopulating() && grid->isEnabled());
    QSignalSpy chosen(&library, &GameList::GameChosen);
    REQUIRE(QMetaObject::invokeMethod(grid, "activated", Qt::DirectConnection,
                                     Q_ARG(QModelIndex, model->index(0, 0))));
    REQUIRE(chosen.count() == 1);
    library.PrepareForLaunch();
    REQUIRE(!model->IsPopulating() && refresh->isEnabled());
    QSignalSpy sorts(model, &QAbstractItemModel::layoutChanged);
    model->AddEntry(entry(QStringLiteral("Renamed")), &parent);
    model->AddEntry(second_entry(), &parent);
    model->DonePopulating({});
    QTest::qWait(20);
    REQUIRE(sorts.count() == 1);
    REQUIRE(model->item(0)->data(GameListItemPath::TitleRole) == QStringLiteral("Zulu"));
    REQUIRE(grid->visualRect(model->index(0, 0)) == first_position);
    QTest::mouseClick(button, Qt::LeftButton);
    REQUIRE(editor->hasFocus());
    QTest::keyClicks(editor, QStringLiteral("Renamed"));
    REQUIRE(editor->text() == QStringLiteral("Renamed"));
    QTest::qWait(250);
    REQUIRE(search->width() == 264);
    QTest::keyClick(editor, Qt::Key_Escape);
    QTest::qWait(250);
    REQUIRE(search->width() == 40 && editor->isHidden());
    button->setFocus(Qt::TabFocusReason);
    REQUIRE(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Escape);
    library.OnPopulate();
    QTest::qWait(250);
    REQUIRE(!button->hasFocus() && !editor->hasFocus() &&
            search->width() == 40);
  }
  REQUIRE(second_source.remove());
  REQUIRE(source.open(QIODevice::Append));
  source.write("changed");
  source.close();
  {
    GameListModel changed({}, nullptr, play_time, system);
    changed.SetFlat(true);
    changed.RestoreSnapshot(UISettings::values.game_dirs);
    REQUIRE(changed.rowCount() == 0);
  }
  QFile corrupt(cache);
  REQUIRE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  corrupt.write("{truncated");
  corrupt.close();
  {
    GameListModel changed({}, nullptr, play_time, system);
    changed.SetFlat(true);
    changed.RestoreSnapshot(UISettings::values.game_dirs);
    REQUIRE(changed.rowCount() == 0);
  }
  {
    GameList library(filesystem, &provider, play_time, system);
    library.resize(900, 650);
    auto* spinner = library.findChild<QWidget*>(QStringLiteral("LibraryLoadingSpinner"));
    auto* grid = library.findChild<GameGrid*>();
    auto* model = qobject_cast<GameListModel*>(library.GetModel());
    library.show();
    library.OnPopulate();
    library.FinishWindowEntrance();
    QTest::qWait(330);
    REQUIRE(spinner->isVisible() && grid->isHidden());
    REQUIRE((spinner->geometry().center() - spinner->parentWidget()->rect().center()).manhattanLength() <= 2);
    model->AddEntry(entry(QStringLiteral("Renamed")), &parent);
    model->DonePopulating({});
    REQUIRE(spinner->isHidden() && grid->isVisible());
  }
  // Base/update dumps of one title must converge regardless of scan order.
  const auto dump_entry = [&](const QString& relative, u64 id = 42, bool complete = true) {
    const QString exefs = root + QLatin1Char('/') + relative + QStringLiteral("/exefs");
    REQUIRE(QDir().mkpath(exefs));
    if (complete) REQUIRE(QDir().mkpath(QFileInfo(exefs).dir().filePath(QStringLiteral("romfs"))));
    QFile main(exefs + QStringLiteral("/main"));
    REQUIRE(main.open(QIODevice::WriteOnly));
    main.write("fixture");
    main.close();
    return QList<QStandardItem*>{
        new GameListItemPath(exefs, {}, QStringLiteral("Fixture"), QStringLiteral("Directory"), id, 0, {}),
        new GameListItem(QStringLiteral("Directory")), new GameListItemSize(7),
        new GameListItemPlayTime(0), new GameListItem(QString{})};
  };
  for (bool reverse : {false, true}) {
    GameListModel model({}, nullptr, play_time, system);
    model.SetFlat(true);
    QFile::remove(cache);
    model.RestoreSnapshot(UISettings::values.game_dirs);
    const auto base = dump_entry(QStringLiteral("title/base"));
    const auto update = dump_entry(QStringLiteral("title/updates/131072"));
    model.AddEntry(reverse ? update : base, &parent);
    model.AddEntry(reverse ? base : update, &parent);
    model.AddEntry(dump_entry(QStringLiteral("title/updates/65536")), &parent);
    model.DonePopulating({});
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.item(0)->data(GameListItemPath::FullPathRole).toString().contains(QStringLiteral("131072")));
  }
  // Older display snapshots also need consolidation before the first visible frame.
  {
    QFile snapshot(cache);
    REQUIRE(snapshot.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(snapshot.readAll()).object();
    snapshot.close();
    auto entries = document[QStringLiteral("entries")].toArray();
    auto base = entries[0].toObject();
    const QString base_path = root + QStringLiteral("/title/base/exefs");
    QFileInfo file(base_path + QStringLiteral("/main"));
    base[QStringLiteral("path")] = base_path;
    base[QStringLiteral("stamp")] = QJsonObject{{QStringLiteral("size"), QString::number(file.size())},
                               {QStringLiteral("modified"), QString::number(file.lastModified().toMSecsSinceEpoch())}};
    entries.append(base);
    document[QStringLiteral("entries")] = entries;
    REQUIRE(snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate));
    snapshot.write(QJsonDocument(document).toJson());
    snapshot.close();
    GameListModel restored({}, nullptr, play_time, system);
    restored.SetFlat(true);
    restored.RestoreSnapshot(UISettings::values.game_dirs);
    REQUIRE(restored.rowCount() == 1);
    REQUIRE(restored.item(0)->data(GameListItemPath::FullPathRole).toString().contains(QStringLiteral("131072")));
  }
  {
    GameListModel model({}, nullptr, play_time, system);
    model.SetFlat(true);
    model.AddEntry(dump_entry(QStringLiteral("title/base")), &parent);
    model.AddEntry(dump_entry(QStringLiteral("other/base")), &parent); // Same ID, separate game folder.
    model.AddEntry(dump_entry(QStringLiteral("title/updates/262144"), 42, false), &parent); // Incomplete patch.
    model.AddEntry(dump_entry(QStringLiteral("title/updates/327680"), 99), &parent); // Different title.
    model.AddEntry(dump_entry(QStringLiteral("unknown/base"), 0), &parent);
    model.AddEntry(dump_entry(QStringLiteral("unknown/updates/131072"), 0), &parent);
    model.DonePopulating({});
    REQUIRE(model.rowCount() == 6);
  }
  std::puts("PASS: deferred RomFS construction and reuse; cached-game activation during scan; "
            "safe listing stop; floating Add placement; Settings signal and toolbar placement");
  std::puts("PASS: loose-dump version selection in both scan orders, cached duplicate consolidation, "
            "separate roots, incomplete updates, distinct titles and unknown IDs preserved");
  std::puts("PASS: staged publication, cache round-trip, stable selection on "
            "refresh, changed-source and corrupt-cache rejection, startup "
            "focus, fade progression, click-to-type, Escape collapse, keyboard "
            "focus, refresh focus, ordered reveal after window fade, single-pass sorting, "
            "quiet background refresh, inactive tree isolation, and centered cold-start spinner");
}
