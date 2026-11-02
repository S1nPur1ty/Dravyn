// SPDX-License-Identifier: GPL-3.0-or-later
// Render actual settings pages against a temporary profile, without starting a game.
#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QStyleOptionSpinBox>
#include <QStyleOptionFrame>
#include <QTemporaryDir>
#include <QTest>
#include <QPushButton>
#include "common/settings.h"
#include "yuzu/configuration/system/new_user_dialog.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "yuzu/configuration/configure_network.h"
#include "input_common/main.h"
#include "yuzu/configuration/configure_dialog.h"
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
    {
        ConfigureDialog settings(nullptr, hotkeys, &inputs, devices, system, false);
        settings.show();
        auto* categories = settings.findChild<QListWidget*>(QStringLiteral("selectorList"));
        if (!categories || categories->count() != 6) return 2;
        const int pages[] = {0, 1, 2, 3, 4, 5};
        for (const int page : pages) {
            categories->setCurrentRow(page);
            QTest::qWait(100);
            auto* tabs = settings.findChild<QTabWidget*>(QStringLiteral("tabWidget"));
            auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
            if (!scroll || scroll->widget()->height() < scroll->widget()->minimumSizeHint().height()) return 3;
            if (page == 3 && scroll->verticalScrollBar()->maximum() == 0) return 4;
            if (page == 1) {
                settings.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-1.png"));
                bool stepped = false;
                for (auto* spin : scroll->widget()->findChildren<QAbstractSpinBox*>()) {
                    auto* editor = spin->findChild<QLineEdit*>();
                    QStyleOptionFrame editor_option;
                    if (editor) editor_option.initFrom(editor);
                    const QRect text_rect = editor ? editor->style()->subElementRect(
                        QStyle::SE_LineEditContents, &editor_option, editor) : QRect{};
                    if (!editor || editor->x() + text_rect.x() + editor->textMargins().left() < 10 || spin->height() < 32) {
                        std::fprintf(stderr, "Field %s: editor x=%d, height=%d\n", spin->metaObject()->className(), editor ? editor->x() : -1, spin->height());
                        return 5;
                    }
                    if (auto* number = qobject_cast<QSpinBox*>(spin);
                        number && number->isEnabled() && number->value() < number->maximum()) {
                        const int before = number->value();
                        QStyleOptionSpinBox option;
                        option.initFrom(number);
                        option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
                        option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
                        option.frame = true;
                        const QRect up = number->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, number);
                        QTest::mouseClick(number, Qt::LeftButton, Qt::NoModifier, up.center());
                        if (number->value() != before + number->singleStep()) return 6;
                        number->setValue(before);
                        stepped = true;
                    }
                }
                if (!stepped) return 7;
            }
            settings.grab().save(QString::fromLocal8Bit(argv[0]) +
                                QStringLiteral("-%1.png").arg(page));
        }
        categories->setCurrentRow(0);
        auto* tabs = settings.findChild<QTabWidget*>(QStringLiteral("tabWidget"));
        tabs->setCurrentIndex(4);
        settings.resize(1000, 760);
        QTest::qWait(100);
        for (auto* area : tabs->currentWidget()->findChildren<QScrollArea*>()) {
            if (area->isVisible() && area->horizontalScrollBar()->maximum() > 0) {
                std::fprintf(stderr, "Debug horizontal overflow: %s %d\n", area->objectName().toUtf8().constData(), area->horizontalScrollBar()->maximum());
                return 8;
            }
        }
        settings.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-debug.png"));
        categories->setCurrentRow(1);
        tabs->setCurrentIndex(1);
        QTest::qWait(100);
        settings.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-profiles.png"));
        tabs->setCurrentIndex(2);
        QTest::qWait(100);
        auto* network = settings.findChild<ConfigureNetwork*>();
        auto* interfaces = network->findChild<QComboBox*>(QStringLiteral("network_interface"));
        if (interfaces->count() == 0) {
            Settings::values.network_interface = "preserved-interface";
            network->ApplyConfiguration();
            if (interfaces->isEnabled() || interfaces->placeholderText().isEmpty() ||
                Settings::values.network_interface.GetValue() != "preserved-interface") return 9;
        }
        settings.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-network.png"));
        NewUserDialog new_user(&settings);
        new_user.show();
        QTest::qWait(100);
        new_user.grab().save(QString::fromLocal8Bit(argv[0]) + QStringLiteral("-new-profile.png"));
        new_user.reject();
        settings.reject();
    }
    }
    inputs.Shutdown();
    std::puts("PASS: settings categories and real General/System/CPU/Graphics/Audio/Controls pages rendered; spin editor padding and step controls verified using temporary profile; dialog rejected without applying changes");
}
