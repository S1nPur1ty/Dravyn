// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include "common/settings.h"
#include "core/core.h"
#include "hid_core/hid_core.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_player_widget.h"
#include "yuzu/configuration/configure_overview.h"
#include "yuzu/configuration/shared_widget.h"
#include "yuzu/util/design_system.h"
#include "yuzu/util/popup.h"

namespace {

// These are views onto the existing editors. Only the original editor serializes on Apply.
class SettingMirror final : public QWidget {
public:
    SettingMirror(ConfigurationShared::Widget* source, QWidget* parent) : QWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        if (source->checkbox) {
            auto* check = new QCheckBox(this);
            check->setAccessibleName(source->checkbox->text());
            check->setToolTip(source->checkbox->text());
            check->setChecked(source->checkbox->isChecked());
            connect(check, &QCheckBox::toggled, source->checkbox, &QCheckBox::setChecked);
            connect(source->checkbox, &QCheckBox::toggled, check, &QCheckBox::setChecked);
            FollowEnabled(source->checkbox, check);
            layout->addWidget(check);
        }
        if (source->combobox) {
            auto* combo = new UIControls::ComboBox(this);
            combo->setModel(source->combobox->model());
            combo->setCurrentIndex(source->combobox->currentIndex());
            combo->setMinimumContentsLength(8);
            combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            connect(combo, &QComboBox::currentIndexChanged, source->combobox,
                    &QComboBox::setCurrentIndex);
            connect(source->combobox, &QComboBox::currentIndexChanged, combo,
                    &QComboBox::setCurrentIndex);
            connect(combo, &QComboBox::activated, source->combobox, &QComboBox::activated);
            FollowEnabled(source->combobox, combo);
            layout->addWidget(combo);
        } else if (source->spinbox) {
            auto* spin = new QSpinBox(this);
            spin->setRange(source->spinbox->minimum(), source->spinbox->maximum());
            spin->setSingleStep(source->spinbox->singleStep());
            spin->setSuffix(source->spinbox->suffix());
            spin->setValue(source->spinbox->value());
            spin->setAlignment(Qt::AlignCenter);
            spin->setMinimumWidth(160);
            connect(spin, &QSpinBox::valueChanged, source->spinbox, &QSpinBox::setValue);
            connect(source->spinbox, &QSpinBox::valueChanged, spin, &QSpinBox::setValue);
            FollowEnabled(source->spinbox, spin);
            layout->addWidget(spin);
        } else if (source->slider) {
            auto* slider = new QSlider(Qt::Horizontal, this);
            slider->setRange(source->slider->minimum(), source->slider->maximum());
            slider->setSingleStep(source->slider->singleStep());
            slider->setValue(source->slider->value());
            slider->setMinimumWidth(140);
            auto* value = new QLabel(QString::number(slider->value()) + QStringLiteral("%"), this);
            value->setMinimumWidth(35);
            connect(slider, &QSlider::valueChanged, source->slider, &QSlider::setValue);
            connect(source->slider, &QSlider::valueChanged, slider, &QSlider::setValue);
            connect(slider, &QSlider::valueChanged, value, [value](int amount) {
                value->setText(QString::number(amount) + QStringLiteral("%"));
            });
            FollowEnabled(source->slider, slider);
            layout->addWidget(slider);
            layout->addWidget(value);
        } else if (source->line_edit) {
            auto* edit = new QLineEdit(source->line_edit->text(), this);
            edit->setValidator(source->line_edit->validator());
            connect(edit, &QLineEdit::textChanged, source->line_edit, &QLineEdit::setText);
            connect(source->line_edit, &QLineEdit::textChanged, edit, &QLineEdit::setText);
            FollowEnabled(source->line_edit, edit);
            layout->addWidget(edit);
        }
        setToolTip(source->toolTip());
    }

private:
    void FollowEnabled(QWidget* source, QWidget* target) {
        UIControls::BindEnabled(source, target);
    }
};

QFrame* Surface(QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setProperty("settingsSurface", true);
    return frame;
}

void AddRow(QVBoxLayout* layout, const QString& title, QWidget* control) {
    auto* row = new QHBoxLayout;
    row->setSpacing(20);
    auto* label = new QLabel(title);
    label->setWordWrap(true);
    row->addWidget(label, 1);
    row->addWidget(control, 1);
    layout->addLayout(row);
}

} // namespace

ConfigureOverview::ConfigureOverview(const ConfigurationShared::Builder& builder,
                                     ConfigureInput& input, Core::System& system, QWidget* parent)
    : QWidget(parent) {
    setAccessibleName(tr("Overview"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 18);
    layout->setSpacing(22);
    auto* heading = new QLabel(tr("Overview"), this);
    heading->setObjectName(QStringLiteral("SettingsHeading"));
    layout->addWidget(heading);
    auto* subtitle = new QLabel(tr("Ready for your next game."), this);
    subtitle->setObjectName(QStringLiteral("SettingsSubtitle"));
    layout->addWidget(subtitle);

    auto* features = new QHBoxLayout;
    features->setSpacing(16);
    auto* display = Surface(this);
    auto* display_layout = new QVBoxLayout(display);
    display_layout->setContentsMargins(22, 18, 22, 18);
    display_layout->addWidget(new QLabel(tr("Display"), display));
    auto* native = new QLabel(tr("Resolution"), display);
    native->setObjectName(QStringLiteral("OverviewValue"));
    display_layout->addStretch();
    display_layout->addWidget(native);
    if (auto* source = builder.FindWidget(&Settings::values.resolution_setup)) {
        if (source->combobox) {
            const auto update_scale = [native](const QString& text) {
                native->setText(
                    text.section(QLatin1Char(' '), 0, 0).replace(QLatin1Char('X'), QChar(0x00d7)));
            };
            update_scale(source->combobox->currentText());
            connect(source->combobox, &QComboBox::currentTextChanged, native, update_scale);
        }
        display_layout->addWidget(new SettingMirror(source, display));
    }
    display_layout->addStretch();
    auto* graphics = new UIControls::Button(tr("Graphics settings"), display);
    graphics->setFlat(true);
    connect(graphics, &QPushButton::clicked, this, &ConfigureOverview::GraphicsRequested);
    display_layout->addWidget(graphics);
    features->addWidget(display, 1);

    auto* controller = Surface(this);
    auto* controller_layout = new QVBoxLayout(controller);
    controller_layout->setContentsMargins(18, 18, 18, 18);
    controller_layout->addWidget(new QLabel(tr("Controller"), controller));
    auto* preview = new PlayerControlPreview(controller);
    preview->setMinimumSize(160, 130);
    preview->setMaximumHeight(155);
    preview->SetController(system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1));
    connect(preview, &PlayerControlPreview::ButtonClicked, this,
            &ConfigureOverview::ControllersRequested);
    connect(preview, &PlayerControlPreview::StickClicked, this,
            &ConfigureOverview::ControllersRequested);
    controller_layout->addWidget(preview);
    auto* controls = new UIControls::Button(tr("Player 1 · Configure"), controller);
    controls->setFlat(true);
    connect(controls, &QPushButton::clicked, this, &ConfigureOverview::ControllersRequested);
    controller_layout->addWidget(controls);
    features->addWidget(controller, 1);
    layout->addLayout(features);

    auto* preferences = Surface(this);
    auto* rows = new QVBoxLayout(preferences);
    rows->setContentsMargins(20, 16, 20, 16);
    rows->setSpacing(18);
    auto* mode = new UIControls::ComboBox(preferences);
    mode->addItems({tr("Handheld"), tr("Docked")});
    mode->setCurrentIndex(input.DockedModeButton()->isChecked() ? 1 : 0);
    UIControls::BindEnabled(input.DockedModeButton(), mode);
    connect(mode, &QComboBox::activated, &input, [&input](int index) {
        (index == 1 ? input.DockedModeButton() : input.HandheldModeButton())->click();
    });
    connect(input.DockedModeButton(), &QRadioButton::toggled, mode,
            [mode](bool docked) { mode->setCurrentIndex(docked ? 1 : 0); });
    AddRow(rows, tr("Console mode"), mode);
    for (auto [setting, title] : {std::pair<Settings::BasicSetting*, QString>{
                                      &Settings::values.speed_limit, tr("Game speed")},
                                  {&Settings::values.volume, tr("Volume")}}) {
        if (auto* source = builder.FindWidget(setting)) {
            AddRow(rows, title, new SettingMirror(source, preferences));
        }
    }
    layout->addWidget(preferences);
    auto* profile = new UIControls::Button(tr("Manage profiles"), this);
    profile->setFlat(true);
    connect(profile, &QPushButton::clicked, this, &ConfigureOverview::ProfilesRequested);
    layout->addWidget(profile, 0, Qt::AlignLeft);
    layout->addStretch();
}
