// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTabBar>
#include <QTreeWidget>
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "configuration/style.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include "yuzu/configuration/change_tracker.h"
#include "yuzu/configuration/configure_applets.h"
#include "yuzu/configuration/configure_audio.h"
#include "yuzu/configuration/configure_cpu.h"
#include "yuzu/configuration/configure_debug_tab.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/configuration/configure_filesystem.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/configuration/configure_graphics.h"
#include "yuzu/configuration/configure_graphics_advanced.h"
#include "yuzu/configuration/configure_graphics_extensions.h"
#include "yuzu/configuration/configure_hotkeys.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_network.h"
#include "yuzu/configuration/configure_overview.h"
#include "yuzu/configuration/configure_profile_manager.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/configuration/configure_ui.h"
#include "yuzu/configuration/configure_web.h"
#include "yuzu/hotkeys.h"
#include "yuzu/util/design_system.h"
#include "yuzu/util/page_transition.h"

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);
    ConfigurationShared::ApplyDialogStyle(this);
    ui->verticalLayout->setContentsMargins(20, 20, 20, 20);
    ui->verticalLayout->setSpacing(18);
    ui->horizontalLayout->setSpacing(18);
    ui->selectorList->setFixedWidth(168);
    ui->selectorList->setSpacing(4);
    setMinimumWidth(960);
    ui->tabWidget->setTabBarAutoHide(true);
    transition = new UIAnimation::PageTransition(
        ui->tabWidget->findChild<QStackedWidget*>(QString{}, Qt::FindDirectChildrenOnly));
    ui->tabWidget->tabBar()->installEventFilter(this);

    ui->tabWidget->addTab(applets_tab.get(), tr("Applets"));
    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));
    ui->tabWidget->addTab(debug_tab_tab.get(), tr("Debug"));
    ui->tabWidget->addTab(filesystem_tab.get(), tr("Filesystem"));
    ui->tabWidget->addTab(general_tab.get(), tr("General"));
    ui->tabWidget->addTab(graphics_tab.get(), tr("Graphics"));
    ui->tabWidget->addTab(graphics_advanced_tab.get(), tr("GraphicsAdvanced"));
    ui->tabWidget->addTab(graphics_extensions_tab.get(), tr("GraphicsExtra"));
    ui->tabWidget->addTab(hotkeys_tab.get(), tr("Hotkeys"));
    ui->tabWidget->addTab(input_tab.get(), tr("Controls"));
    ui->tabWidget->addTab(profile_tab.get(), tr("Profiles"));
    ui->tabWidget->addTab(network_tab.get(), tr("Network"));
    ui->tabWidget->addTab(system_tab.get(), tr("System"));
    ui->tabWidget->addTab(ui_tab.get(), tr("Game List"));
    ui->tabWidget->addTab(web_tab.get(), tr("Web"));

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);
    overview_tab = std::make_unique<ConfigureOverview>(*builder, *input_tab, system, this);
    connect(overview_tab.get(), &ConfigureOverview::GraphicsRequested, this,
            [this] { ui->selectorList->setCurrentRow(3); });
    connect(overview_tab.get(), &ConfigureOverview::ControllersRequested, this,
            [this] { ui->selectorList->setCurrentRow(2); });
    connect(overview_tab.get(), &ConfigureOverview::ProfilesRequested, this, [this] {
        ui->selectorList->setCurrentRow(5);
        ui->tabWidget->setCurrentIndex(1);
    });

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();
    PopulateSelectionList();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != -1) {
            debug_tab_tab->SetCurrentIndex(0);
            transition->Start();
        }
    });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);
    connect(ui->selectorList, &QListWidget::itemSelectionChanged, this,
            &ConfigureDialog::UpdateVisibleTabs);

    // Keep a stable header height when there are no pending edits.
    for (auto standard : {QDialogButtonBox::Cancel, QDialogButtonBox::Apply}) {
        auto* original = ui->buttonBox->button(standard);
        auto* button = new UIControls::Button(original->text(), ui->buttonBox);
        const auto role = ui->buttonBox->buttonRole(original);
        ui->buttonBox->removeButton(original);
        delete original;
        ui->buttonBox->addButton(button, role);
        if (standard == QDialogButtonBox::Apply) {
            button->setDefault(true);
            connect(button, &QAbstractButton::clicked, this,
                    &ConfigureDialog::HandleApplyButtonClicked);
        }
    }
    auto policy = ui->buttonBox->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    ui->buttonBox->setSizePolicy(policy);
    changes = new ConfigurationShared::ChangeTracker(
        this, [this](bool dirty) { ui->buttonBox->setVisible(dirty); });
    changes->Watch(this);
    connect(input_tab.get(), &ConfigureInput::ChildConfigurationChanged, changes,
            &ConfigurationShared::ChangeTracker::ChildChangesCommitted);
    for (auto* player : findChildren<ConfigureInputPlayer*>()) {
        changes->Add(player, [player] { return player->BindingState(); });
        connect(player, &ConfigureInputPlayer::BindingsChanged, changes,
                &ConfigurationShared::ChangeTracker::ScheduleUpdate);
    }
    // Hotkey edits live in a model rather than a persistent line editor.
    for (auto* tree : hotkeys_tab->findChildren<QTreeWidget*>()) {
        changes->Add(tree, [tree] {
            QStringList values;
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                for (int column = 0; column < tree->columnCount(); ++column) {
                    values.append((*it)->text(column));
                }
                ++it;
            }
            return values;
        });
        connect(tree->model(), &QAbstractItemModel::dataChanged, changes,
                &ConfigurationShared::ChangeTracker::ScheduleUpdate);
    }
    changes->Accept();

    adjustSize();
    ui->selectorList->setCurrentRow(0);

    ui->selectorList->setFocus();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_row = ui->selectorList->currentRow();
    const int old_index = ui->tabWidget->currentIndex();

    ui->retranslateUi(this);

    PopulateSelectionList();
    ui->selectorList->setCurrentRow(old_row);

    UpdateVisibleTabs();
    ui->tabWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
    changes->Accept();
}

Q_DECLARE_METATYPE(QList<QWidget*>);

void ConfigureDialog::PopulateSelectionList() {
    const std::array<std::pair<QString, QList<QWidget*>>, 7> items{{
        {tr("Overview"), {overview_tab.get()}},
        {tr("General"),
         {general_tab.get(), hotkeys_tab.get(), ui_tab.get(), filesystem_tab.get(), web_tab.get()}},
        {tr("Controllers"), input_tab->GetSubTabs()},
        {tr("Graphics"),
         {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()}},
        {tr("Audio"), {audio_tab.get()}},
        {tr("System"), {system_tab.get(), profile_tab.get(), network_tab.get(), applets_tab.get()}},
        {tr("Advanced"), {cpu_tab.get(), debug_tab_tab.get()}},
    }};

    [[maybe_unused]] const QSignalBlocker blocker(ui->selectorList);

    ui->selectorList->clear();
    for (const auto& entry : items) {
        auto* const item = new QListWidgetItem(entry.first);
        item->setData(Qt::UserRole, QVariant::fromValue(entry.second));

        ui->selectorList->addItem(item);
    }
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    //  Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::UpdateVisibleTabs() {
    const auto items = ui->selectorList->selectedItems();
    if (items.isEmpty()) {
        return;
    }

    [[maybe_unused]] const QSignalBlocker blocker(ui->tabWidget);

    transition->Capture();
    ui->tabWidget->setUpdatesEnabled(false);
    ui->tabWidget->clear();

    const auto tabs = qvariant_cast<QList<QWidget*>>(items[0]->data(Qt::UserRole));

    for (auto* const tab : tabs) {
        LOG_DEBUG(Frontend, "{}", tab->accessibleName().toStdString());
        auto* scroll = scroll_pages.value(tab);
        if (!scroll) {
            scroll = ConfigurationShared::CreateScrollablePage(tab, ui->tabWidget);
            scroll_pages.insert(tab, scroll);
        }
        ui->tabWidget->addTab(scroll, tab->accessibleName());
    }
    ui->tabWidget->setUpdatesEnabled(true);
    transition->Start();
}

bool ConfigureDialog::eventFilter(QObject* object, QEvent* event) {
    if (object == ui->tabWidget->tabBar() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress)) {
        transition->Capture();
    }
    return QDialog::eventFilter(object, event);
}

void ConfigureDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (!event->spontaneous() && UIAnimation::IsEnabled(this)) {
        setWindowOpacity(0.0);
        auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(300);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ConfigureDialog::done(int result) {
    if (closing) {
        return;
    }
    if (!isVisible() || !UIAnimation::IsEnabled(this)) {
        QDialog::done(result);
        return;
    }
    closing = true;
    auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(150);
    animation->setStartValue(windowOpacity());
    animation->setEndValue(0.0);
    connect(animation, &QPropertyAnimation::finished, this,
            [this, result] { QDialog::done(result); });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}
