// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/frontend/applet_software_keyboard_types.h"
#include "core/hle/service/am/frontend/applets.h"

using namespace Service::AM::Frontend;

TEST_CASE("Switch 2 keyboard decodes the foreground wire layout", "[software-keyboard]") {
    // Offsets from SDK ShowKeyboard (revision 0x140010), independent of the C++ layout.
    std::array<u8, 0x530> wire{};
    const auto put32 = [&](size_t offset, u32 value) {
        std::memcpy(wire.data() + offset, &value, sizeof(value));
    };
    const std::u16string title = u"Enter a name";
    std::memcpy(wire.data() + 0x24, title.data(), title.size() * sizeof(char16_t));
    put32(0, 4);
    put32(0x20, 1);
    put32(0x3AC, 12);
    put32(0x3B0, 1);
    wire[0x1B] = 1;
    wire[0x3AA] = 1;
    wire[0x3AB] = 1;
    put32(0x3BC, 0x14);
    put32(0x3C0, 3);
    put32(0x3C4, 0x800);
    put32(0x3C8, 2);
    wire[0x48D] = 1;
    wire[0x48E] = 1;
    put32(0x490, 4);
    SwkbdConfigNx2 config{};
    std::memcpy(&config, wire.data(), wire.size());
    const auto common = config.ToCommon();
    const auto extra = config.ToNew();
    CHECK(common.type == SwkbdType::Latin);
    CHECK(common.initial_cursor_position == SwkbdInitialCursorPosition::End);
    CHECK(std::u16string(common.header_text.data()) == title);
    CHECK(common.max_text_length == 12);
    CHECK(common.min_text_length == 1);
    CHECK(common.enable_return_button);
    CHECK(common.use_utf8);
    CHECK(common.use_blur_background);
    CHECK(common.use_text_check);
    CHECK(common.initial_string_offset == 0x14);
    CHECK(common.initial_string_length == 3);
    CHECK(common.user_dictionary_offset == 0x800);
    CHECK(common.user_dictionary_entries == 2);
    CHECK(extra.disable_cancel_button);
    CHECK(extra.text_grouping[0] == 4);

    SECTION("UTF-16 and unchecked requests do not inherit flags") {
        wire[0x3AA] = wire[0x48D] = wire[0x48E] = 0;
        std::memcpy(&config, wire.data(), wire.size());
        CHECK_FALSE(config.ToCommon().use_utf8);
        CHECK_FALSE(config.ToCommon().use_text_check);
        CHECK_FALSE(config.ToNew().disable_cancel_button);
    }
}

TEST_CASE("Keyboard legacy and Switch 2 layouts stay distinct", "[software-keyboard]") {
    CHECK(sizeof(SwkbdConfigCommon) + sizeof(SwkbdConfigNew) == 0x4C8);
    CHECK(offsetof(SwkbdConfigCommon, use_utf8) == 0x3BD);
    CHECK(offsetof(SwkbdConfigCommon, use_text_check) == 0x3D0);
    CHECK(static_cast<u32>(Service::AM::AppletId::SoftwareKeyboard) == 0x11);
    CHECK(static_cast<u32>(Service::AM::AppletId::SoftwareKeyboardNx2) == 0x01020011);
}

namespace {
class TestKeyboard final : public Core::Frontend::SoftwareKeyboardApplet {
public:
    mutable size_t shown{};
    void Close() const override {}
    void InitializeKeyboard(bool, Core::Frontend::KeyboardInitializeParameters,
                            SubmitNormalCallback, SubmitInlineCallback) override {}
    void ShowNormalKeyboard() const override {
        ++shown;
    }
    void ShowTextCheckDialog(SwkbdTextCheckResult, std::u16string) const override {}
    void ShowInlineKeyboard(Core::Frontend::InlineAppearParameters) const override {}
    void HideInlineKeyboard() const override {}
    void InlineTextChanged(Core::Frontend::InlineTextParameters) const override {}
    void ExitKeyboard() const override {}
};
} // namespace

TEST_CASE("Both foreground keyboard IDs dispatch to the configured frontend",
          "[software-keyboard]") {
    Core::System system;
    FrontendAppletHolder holder{system};
    FrontendAppletSet set;
    auto frontend = std::make_unique<TestKeyboard>();
    auto& keyboard = *frontend;
    set.software_keyboard = std::move(frontend);
    holder.SetFrontendAppletSet(std::move(set));
    for (const auto id :
         {Service::AM::AppletId::SoftwareKeyboard, Service::AM::AppletId::SoftwareKeyboardNx2}) {
        auto applet = holder.GetApplet({}, id, Service::AM::LibraryAppletMode::AllForeground);
        REQUIRE(applet);
        // Dispatch/UI routing needs no guest process or storage initialization.
        applet->Execute();
    }
    CHECK(keyboard.shown == 2);
}
