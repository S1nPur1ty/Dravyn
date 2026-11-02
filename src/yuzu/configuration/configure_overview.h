// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class ConfigureInput;
namespace Core {
class System;
}
namespace ConfigurationShared {
class Builder;
}

class ConfigureOverview final : public QWidget {
    Q_OBJECT
public:
    explicit ConfigureOverview(const ConfigurationShared::Builder& builder, ConfigureInput& input,
                               Core::System& system, QWidget* parent);

signals:
    void GraphicsRequested();
    void ControllersRequested();
    void ProfilesRequested();
};
