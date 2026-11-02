// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QTimer>
#include <QBuffer>
#include <QByteArray>
#include <QGraphicsOpacityEffect>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QStyleOption>
#include "common/container/unordered_map.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"
#include "yuzu/loading_screen.h"
#include "yuzu/util/design_system.h"

// Mingw seems to not have QMovie at all. If QMovie is missing then use a single frame instead of an
// showing the full animation
#if !YUZU_QT_MOVIE_MISSING
#include <QMovie>
#endif

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    UIControls::ApplyPalette(this);
    setAutoFillBackground(true);
    // Style the surface explicitly, as Settings does, instead of inheriting the native theme.
    setStyleSheet(QStringLiteral("QWidget#LoadingScreen { background: %1; }"
                                "QWidget#fade_parent, QWidget#progressContent {"
                                " background: transparent; }")
                      .arg(UIControls::Colors::Background.name()));
    ui->fade_parent->setAutoFillBackground(false);
    ui->stage->setStyleSheet(QStringLiteral("color: %1; font-size: 20px; font-weight: 600;")
                                 .arg(UIControls::Colors::Text.name()));
    const auto secondary_style = QStringLiteral("color: %1; font-size: 12px;")
                                     .arg(UIControls::Colors::Muted.name());
    ui->details->setStyleSheet(secondary_style);
    ui->value->setStyleSheet(secondary_style);
    ui->progress_bar->setStyleSheet(
        QStringLiteral("QProgressBar { background: %1; border: none; border-radius: 4px; }"
                       "QProgressBar::chunk { background: %2; border-radius: 4px; }")
            .arg(UIControls::Colors::Control.name(), UIControls::Colors::Accent.name()));
    auto progress_policy = ui->progress_bar->sizePolicy();
    progress_policy.setRetainSizeWhenHidden(true);
    ui->progress_bar->setSizePolicy(progress_policy);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    // Fade the content over the same background before handing over to the game.
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity_effect, "opacity");
    fadeout_animation->setDuration(500);
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    fadeout_animation->setEasingCurve(QEasingCurve::InOutCubic);

    // After the fade completes, hide the widget and reset the opacity
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, [this] {
        hide();
        opacity_effect->setOpacity(1);
        emit Hidden();
    });
    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);
    qRegisterMetaType<VideoCore::LoadCallbackStage>();

    stage_translations = {
        {VideoCore::LoadCallbackStage::Prepare, tr("Preparing your game")},
        {VideoCore::LoadCallbackStage::Build, tr("Preparing shaders")},
        {VideoCore::LoadCallbackStage::RestoreCache, tr("Restoring shader cache")},
        {VideoCore::LoadCallbackStage::ReadCache, tr("Reading shader cache")},
        {VideoCore::LoadCallbackStage::SaveCache, tr("Saving shader cache")},
        {VideoCore::LoadCallbackStage::Complete, tr("Starting your game")},
    };
    progress_timer = new QTimer(this);
    progress_timer->setInterval(250);
    connect(progress_timer, &QTimer::timeout, this, &LoadingScreen::UpdateElapsedTime);
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::Prepare(Loader::AppLoader& loader) {
    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
#ifdef YUZU_QT_MOVIE_MISSING
        QPixmap map;
        map.loadFromData(buffer.data(), buffer.size());
        ui->banner->setPixmap(map);
#else
        backing_mem = std::make_unique<QByteArray>(reinterpret_cast<char*>(buffer.data()),
                                                   static_cast<int>(buffer.size()));
        backing_buf = std::make_unique<QBuffer>(backing_mem.get());
        backing_buf->open(QIODevice::ReadOnly);
        animation = std::make_unique<QMovie>(backing_buf.get(), QByteArray());
        animation->start();
        ui->banner->setMovie(animation.get());
#endif
        buffer.clear();
    }
    if (loader.ReadLogo(buffer) == Loader::ResultStatus::Success) {
        QPixmap map;
        map.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
        ui->logo->setPixmap(map);
    }

    previous_stage = VideoCore::LoadCallbackStage::Complete;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::OnLoadComplete() {
    progress_timer->stop();
    fadeout_animation->start(QPropertyAnimation::KeepWhenStopped);
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    if (stage != previous_stage) {
        stage_start = std::chrono::steady_clock::now();
        stage_first_value = value;
        previous_stage = stage;
    }
    previous_value = value;
    previous_total = total;
    const bool counted = stage == VideoCore::LoadCallbackStage::Build && total > 0;
    ui->progress_bar->setVisible(counted);
    if (counted) {
        ui->progress_bar->setRange(0, static_cast<int>(total));
        ui->progress_bar->setValue(static_cast<int>(value));
        ui->details->setText(tr("%1 of %2").arg(locale().toString(qulonglong(value)))
                                             .arg(locale().toString(qulonglong(total))));
    } else if (stage == VideoCore::LoadCallbackStage::ReadCache) {
        ui->details->setText(tr("%1 pipelines read").arg(locale().toString(qulonglong(value))));
    } else {
        ui->details->clear();
    }
    ui->stage->setText(stage_translations[stage]);
    if (stage == VideoCore::LoadCallbackStage::Complete) progress_timer->stop();
    else progress_timer->start();
    UpdateElapsedTime();
}

void LoadingScreen::UpdateElapsedTime() {
    using namespace std::chrono;
    const auto elapsed = duration_cast<seconds>(steady_clock::now() - stage_start).count();
    const auto format_time = [](qint64 seconds) {
        return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    };
    QString status = tr("Elapsed %1").arg(format_time(elapsed));
    // No estimate until at least one shader completes; a stalled count must not divide by zero.
    if (previous_stage == VideoCore::LoadCallbackStage::Build && elapsed >= 1 &&
        previous_value > stage_first_value && previous_total > previous_value) {
        const auto remaining = static_cast<qint64>(
            double(previous_total - previous_value) * elapsed / (previous_value - stage_first_value));
        status += tr(" — Estimated remaining %1").arg(format_time(remaining));
    }
    ui->value->setText(status);
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

void LoadingScreen::Clear() {
    progress_timer->stop();
#ifndef YUZU_QT_MOVIE_MISSING
    animation.reset();
    backing_buf.reset();
    backing_mem.reset();
#endif
}
