// SPDX-License-Identifier: GPL-3.0-or-later
// Offscreen widget regression; does not create an emulator or load a game.
#include "common/container/unordered_map.h"
#include "video_core/rasterizer_interface.h"
#include "yuzu/loading_screen.h"
#include "moc_loading_screen.cpp"
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVariantAnimation>
#include <cstdio>
#include <cstdlib>
#define REQUIRE(test) do { if (!(test)) { std::fprintf(stderr, "%s failed at line %d\n", #test, __LINE__); std::exit(1); } } while (0)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    LoadingScreen screen;
    auto* bar = screen.findChild<QProgressBar*>(QStringLiteral("progress_bar"));
    auto* label = screen.findChild<QLabel*>(QStringLiteral("value"));
    auto* stage = screen.findChild<QLabel*>(QStringLiteral("stage"));
    REQUIRE(bar && label && stage);
    auto* spinner = screen.findChild<QWidget*>(QStringLiteral("loading_spinner"));
    REQUIRE(spinner);
    auto* rotation = spinner->findChild<QVariantAnimation*>();
    REQUIRE(rotation);
    screen.show();
    using Stage = VideoCore::LoadCallbackStage;
    const auto wait = [] {
        QEventLoop loop;
        QTimer::singleShot(1250, &loop, &QEventLoop::quit);
        loop.exec();
    };
    screen.OnLoadProgress(Stage::RestoreCache, 0, 0);
    REQUIRE(bar->maximum() == 0);
    REQUIRE(stage->text().contains(QStringLiteral("Restoring")));
    const auto first_frame = spinner->grab().toImage();
    REQUIRE(rotation->state() == QAbstractAnimation::Running);
    wait();
    REQUIRE(spinner->grab().toImage() != first_frame);
    REQUIRE(label->text().contains(QStringLiteral("0:01")));
    screen.OnLoadProgress(Stage::ReadCache, 64, 0);
    REQUIRE(stage->text().contains(QStringLiteral("64")));
    screen.OnLoadProgress(Stage::Build, 0, 100);
    wait();
    REQUIRE(!label->text().contains(QStringLiteral("Estimated")));
    screen.OnLoadProgress(Stage::Build, 10, 100);
    REQUIRE(bar->value() == 10 && bar->maximum() == 100);
    REQUIRE(label->text().contains(QStringLiteral("Estimated")));
    screen.OnLoadProgress(Stage::SaveCache, 0, 0);
    REQUIRE(bar->minimum() == 0 && bar->maximum() == 0);
    screen.OnLoadProgress(Stage::Complete, 0, 0);
    for (auto* timer : screen.findChildren<QTimer*>()) REQUIRE(!timer->isActive());
    REQUIRE(rotation->state() == QAbstractAnimation::Running); // Still launching until first frame.
    screen.OnLoadComplete();
    wait();
    REQUIRE(!screen.isVisible());
    REQUIRE(rotation->state() == QAbstractAnimation::Stopped);
    screen.OnLoadProgress(Stage::Prepare, 0, 0);
    screen.show();
    REQUIRE(rotation->state() == QAbstractAnimation::Running);
    screen.Clear();
    REQUIRE(spinner->isHidden() && rotation->state() == QAbstractAnimation::Stopped);
    std::puts("PASS: restore/read/build/save stages, busy indicator, elapsed heartbeat, zero-progress ETA guard, timer cleanup, animated spinner frames, hide/show lifecycle, and cancellation cleanup");
}
