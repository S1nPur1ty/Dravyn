// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual Controls page with an isolated profile; never launch a game.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QStyleOptionGroupBox>
#include <QStyleOptionFrame>
#include <QWheelEvent>
#include <QListView>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <qtestwheel.h>
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "hid_core/hid_core.h"
#include "input_common/main.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_vibration.h"
#include "yuzu/configuration/configure_motion_touch.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_input_player_widget.h"
#include "yuzu/hotkeys.h"
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir profile;
    if (!profile.isValid()) return 1;
    for (int i = int(Common::FS::EdenPath::EdenDir); i <= int(Common::FS::EdenPath::IconsDir); ++i) {
        const QString path = profile.path() + QStringLiteral("/%1").arg(i);
        QDir().mkpath(path);
        Common::FS::SetEdenPath(static_cast<Common::FS::EdenPath>(i), path.toStdString());
    }
    Common::Log::Initialize();
    InputCommon::InputSubsystem inputs;
    inputs.Initialize();
    {
        Core::System system;
        HotkeyRegistry hotkeys;
        std::vector<VkDeviceInfo::Record> devices;
        ConfigureDialog settings(nullptr, hotkeys, &inputs, devices, system, false);
        settings.show();
        settings.findChild<QListWidget*>(QStringLiteral("selectorList"))->setCurrentRow(5);
        QTest::qWait(100);
        ConfigureInputPlayer* player = nullptr;
        for (auto* candidate : settings.findChildren<ConfigureInputPlayer*>()) {
            if (candidate->isVisible()) player = candidate;
        }
        if (!player) return 2;
        auto* type = player->findChild<QComboBox*>(QStringLiteral("comboControllerType"));
        type->setCurrentIndex(type->findText(QStringLiteral("Pro Controller")));
        auto* preview = player->findChild<PlayerControlPreview*>();
        auto* group = player->findChild<QComboBox*>(QStringLiteral("bindingGroup"));
        auto point = [preview](QPointF image) {
            QSizeF size(1536, 1024);
            size.scale(preview->contentsRect().size(), Qt::KeepAspectRatio);
            const QPointF origin = QPointF(preview->contentsRect().center()) -
                                   QPointF(size.width(), size.height()) / 2;
            return (origin + image * (size.width() / 1536)).toPoint();
        };
        for (const int width : {1400, 1000}) {
            settings.resize(width, 900);
            QTest::qWait(100);
            for (int page = 0; page < 6; ++page) {
                group->setCurrentIndex(page);
                QTest::qWait(25);
                for (auto* scroll : settings.findChildren<QScrollArea*>()) {
                    if (scroll->isVisible() && scroll->horizontalScrollBar()->maximum() > 0) {
                        std::fprintf(stderr, "Horizontal overflow at %d, page %d: %d\n", width, page,
                                     scroll->horizontalScrollBar()->maximum());
                        for (auto* widget : player->findChildren<QWidget*>()) {
                            if (widget->minimumSizeHint().width() > 500 || widget->minimumWidth() > 500) {
                                std::fprintf(stderr, "  %s width %d min %d hint %d\n", widget->objectName().toUtf8().constData(), widget->width(), widget->minimumWidth(), widget->minimumSizeHint().width());
                            }
                        }
                        std::fprintf(stderr, "player width=%d min=%d hint=%d viewport=%d\n", player->width(), player->minimumWidth(), player->minimumSizeHint().width(), scroll->viewport()->width());
                        return 3;
                    }
                }
            }
            group->setCurrentIndex(0);
            settings.grab().save(QString::fromLocal8Bit(argv[0]) +
                                QStringLiteral("-%1.png").arg(width));
        }
        settings.resize(1400, 900);
        QTest::qWait(100);
        QSignalSpy buttons(preview, &PlayerControlPreview::ButtonClicked);
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, point({1259, 452}));
        auto* a = player->findChild<QPushButton*>(QStringLiteral("buttonA"));
        if (buttons.count() != 1 || buttons[0][0].toInt() != Settings::NativeButton::A ||
            !a->text().contains(QStringLiteral("waiting"))) return 4;
        // Let the normal mapping timeout cancel; no controller settings are applied.
        if (!QTest::qWaitFor([a] { return !a->text().contains(QStringLiteral("waiting")); }, 5000)) return 5;
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, point({378, 449}));
        if (group->currentIndex() != 1) return 6;
        QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, point({986, 657}));
        if (group->currentIndex() != 2) return 7;
        // Test both directions through the real HID callback, not just a decorative animation.
        auto* controller = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
        const QImage neutral = preview->grab().toImage();
        inputs.GetVirtualGamepad()->SetStickPosition(0, 0, 1.0f, 0.5f);
        inputs.GetVirtualGamepad()->SetStickPosition(0, 1, 1.0f, 0.5f);
        QTest::qWait(40);
        if (preview->grab().toImage() == neutral) return 8;
        preview->grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-sticks.png"));
        // An idle Controls page must propagate wheel events to the containing scroll area.
        auto* tabs = settings.findChild<QTabWidget*>(QStringLiteral("tabWidget"));
        auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
        if (!scroll || scroll->verticalScrollBar()->maximum() <= 0) return 9;
        scroll->verticalScrollBar()->setValue(0);
        const QPoint wheel_position = preview->mapTo(&settings, preview->rect().center());
        QTest::wheelEvent(settings.windowHandle(), wheel_position, QPoint(0, -120));
        if (scroll->verticalScrollBar()->value() <= 0) return 10;
        const int after_down = scroll->verticalScrollBar()->value();
        QTest::wheelEvent(settings.windowHandle(), wheel_position, QPoint(0, 120));
        if (scroll->verticalScrollBar()->value() >= after_down) return 11;

        auto check_group_indicators = [](QWidget* parent) {
            for (auto* group : parent->findChildren<QGroupBox*>()) {
                if (!group->isCheckable() || !group->isVisible()) continue;
                QStyleOptionGroupBox option;
                option.initFrom(group);
                option.text = group->title();
                option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel |
                                     QStyle::SC_GroupBoxCheckBox;
                const QRect indicator = group->style()->subControlRect(
                    QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxCheckBox, group);
                if (!group->rect().contains(indicator)) {
                    std::fprintf(stderr, "Clipped indicator %s y=%d height=%d\n",
                                 group->objectName().toUtf8().constData(), indicator.y(), indicator.height());
                    return false;
                }
            }
            return true;
        };
        if (!check_group_indicators(&settings)) return 12;
        {
            for (auto& configuration : Settings::values.players.GetValue()) {
                configuration.vibration_enabled = true;
                configuration.vibration_strength = 100;
            }
            ConfigureVibration vibration(&settings, system.HIDCore());
            vibration.show();
            QTest::qWait(50);
            if (!check_group_indicators(&vibration)) return 13;
            for (auto* spin : vibration.findChildren<QSpinBox*>()) {
                auto* editor = spin->findChild<QLineEdit*>();
                QStyleOptionFrame option;
                option.initFrom(editor);
                const QRect content = editor->style()->subElementRect(QStyle::SE_LineEditContents, &option, editor);
                const int room = content.width() - editor->textMargins().left() - editor->textMargins().right();
                if (room < editor->fontMetrics().horizontalAdvance(spin->text())) {
                    std::fprintf(stderr, "Cramped percentage %s available=%d\n", spin->objectName().toUtf8().constData(), room);
                    return 14;
                }
            }
            vibration.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-vibration.png"));
            vibration.reject();
        }
        {
            Settings::values.udp_input_servers = "127.0.0.1:26760";
            ConfigureMotionTouch motion(&settings, &inputs);
            motion.show();
            QTest::qWait(50);
            auto* list = motion.findChild<QListView*>(QStringLiteral("udp_server_list"));
            list->setCurrentIndex(list->model()->index(0, 0));
            const QRect row = list->visualRect(list->currentIndex());
            if (row.height() < 40 || list->viewport()->geometry().top() < 8) return 15;
            motion.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-motion.png"));
            motion.reject();
        }
        settings.reject();
    }
    inputs.Shutdown();
    std::puts("PASS: responsive canvas, illustrated A mapping/timeout, both stick inspectors, HID-driven stick movement; wheel scrolling both ways, unclipped group indicators, readable vibration percentages and inset motion server rows; temporary profile only.");
}
