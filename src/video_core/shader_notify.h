// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

namespace VideoCore {
class ShaderNotify {
public:
    [[nodiscard]] int ShadersBuilding() noexcept;

    void MarkShaderComplete() noexcept {
        ++num_complete;
    }

    void MarkShaderBuilding() noexcept {
        ++num_building;
    }

private:
    std::atomic_int num_building{};
    std::atomic_int num_complete{};

};
} // namespace VideoCore
