// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "video_core/shader_notify.h"

namespace VideoCore {
int ShaderNotify::ShadersBuilding() noexcept {
    const int complete = num_complete.load(std::memory_order_relaxed);
    const int building = num_building.load(std::memory_order_relaxed);
    return std::max(0, building - complete);
}
} // namespace VideoCore
