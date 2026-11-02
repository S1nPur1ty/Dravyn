// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPointer>
#include <QSpinBox>
#include <QTimer>
#include <QVariant>

namespace ConfigurationShared {

// Compare editor values with the last applied state. Navigation and returning an editor to
// its original value do not create pending changes. Signals are coalesced after dependent
// controls have finished updating; no polling or writes to the settings store are needed.
class ChangeTracker final : public QObject {
public:
    ChangeTracker(QObject* parent, std::function<void(bool)> changed)
        : QObject(parent), changed{std::move(changed)} {}

    void Watch(QWidget* root) {
        for (auto* widget : root->findChildren<QWidget*>()) {
            if (widget->property("settingsNavigation").toBool()) {
                continue;
            }
            if (auto* button = qobject_cast<QAbstractButton*>(widget);
                button && button->isCheckable()) {
                Add(button, [button] { return button->isChecked(); });
                connect(button, &QAbstractButton::toggled, this, &ChangeTracker::ScheduleUpdate);
            } else if (auto* group = qobject_cast<QGroupBox*>(widget);
                       group && group->isCheckable()) {
                Add(group, [group] { return group->isChecked(); });
                connect(group, &QGroupBox::toggled, this, &ChangeTracker::ScheduleUpdate);
            } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                Add(combo, [combo] { return combo->currentText(); });
                connect(combo, &QComboBox::currentTextChanged, this,
                        &ChangeTracker::ScheduleUpdate);
            } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
                Add(spin, [spin] { return spin->value(); });
                connect(spin, &QSpinBox::valueChanged, this, &ChangeTracker::ScheduleUpdate);
            } else if (auto* spin = qobject_cast<QDoubleSpinBox*>(widget)) {
                Add(spin, [spin] { return spin->value(); });
                connect(spin, &QDoubleSpinBox::valueChanged, this, &ChangeTracker::ScheduleUpdate);
            } else if (auto* date = qobject_cast<QDateTimeEdit*>(widget)) {
                Add(date, [date] { return date->dateTime(); });
                connect(date, &QDateTimeEdit::dateTimeChanged, this,
                        &ChangeTracker::ScheduleUpdate);
            } else if (auto* slider = qobject_cast<QAbstractSlider*>(widget)) {
                // Scroll position is navigation, not a setting.
                if (widget->inherits("QScrollBar")) {
                    continue;
                }
                Add(slider, [slider] { return slider->value(); });
                connect(slider, &QAbstractSlider::valueChanged, this,
                        &ChangeTracker::ScheduleUpdate);
            } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
                if (qobject_cast<QAbstractSpinBox*>(edit->parentWidget()) ||
                    qobject_cast<QComboBox*>(edit->parentWidget())) {
                    continue;
                }
                Add(edit, [edit] { return edit->text(); });
                connect(edit, &QLineEdit::textChanged, this, &ChangeTracker::ScheduleUpdate);
            }
        }
    }

    void Add(QObject* owner, std::function<QVariant()> read) {
        entries.push_back({owner, std::move(read), {}});
    }

    void Accept() {
        committed_child_changes = false;
        for (auto& entry : entries) {
            if (entry.owner) {
                entry.baseline = entry.read();
            }
        }
        changed(false);
    }

    bool IsDirty() const {
        if (committed_child_changes) {
            return true;
        }
        for (const auto& entry : entries) {
            if (entry.owner && entry.read() != entry.baseline) {
                return true;
            }
        }
        return false;
    }

    // A child dialog has already accepted its edits; retain the parent save action until Apply.
    void ChildChangesCommitted() {
        committed_child_changes = true;
        ScheduleUpdate();
    }

    void ScheduleUpdate() {
        if (pending) {
            return;
        }
        pending = true;
        QTimer::singleShot(0, this, [this] {
            pending = false;
            changed(IsDirty());
        });
    }

private:
    struct Entry {
        QPointer<QObject> owner;
        std::function<QVariant()> read;
        QVariant baseline;
    };
    std::vector<Entry> entries;
    std::function<void(bool)> changed;
    bool pending{};
    bool committed_child_changes{};
};

} // namespace ConfigurationShared
