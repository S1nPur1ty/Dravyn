// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "audio_core/audio_out_manager.h"
#include "audio_core/out/audio_out.h"
#include "core/core.h"

TEST_CASE("Audio output unregisters and finalizes its session", "[audio-out]") {
    Core::System system;
    AudioCore::AudioOut::Manager manager{system};
    for (size_t i = 0; i < AudioCore::AudioOut::MaxOutSessions * 2; ++i) {
        size_t id{};
        REQUIRE(manager.AcquireSessionId(id).IsSuccess());
        auto out = std::make_shared<AudioCore::AudioOut::Out>(system, manager, nullptr, id);
        manager.sessions[id] = out;
        manager.applet_resource_user_ids[id] = 1;
        out->Free();
        CHECK(manager.sessions[id] == nullptr);
        CHECK(manager.applet_resource_user_ids[id] == 0);
        CHECK(manager.num_free_sessions == AudioCore::AudioOut::MaxOutSessions);
        // The service may release its last reference after the manager has removed the session.
        out.reset();
    }
}
