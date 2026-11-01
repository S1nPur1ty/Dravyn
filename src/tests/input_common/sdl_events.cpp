// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include "common/param_package.h"
#include "input_common/drivers/sdl_driver.h"

TEST_CASE("SDL mapped events cannot overwrite raw controller bindings", "[sdl-input]") {
    InputCommon::SDLDriver driver{"sdl"};
    SDL_VirtualJoystickDesc desc{};
    desc.version = sizeof(desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 16;
    desc.nhats = 1;
    desc.name = "Eglantine input regression";
    const auto id = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(id != 0);
    struct Detach {
        SDL_JoystickID id;
        ~Detach() {
            SDL_DetachVirtualJoystick(id);
        }
    } detach{id};
    char guid[33];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    const auto mapping = std::string{guid} +
                         ",Eglantine input regression,a:b2,b:b0,x:b3,y:b1,"
                         "leftx:a2,lefty:a3,rightx:a0,righty:a1,lefttrigger:a4,righttrigger:a5,";
    REQUIRE(SDL_AddGamepadMapping(mapping.c_str()) >= 0);
    driver.PumpEvents();
    auto* joystick = SDL_GetJoystickFromID(id);
    REQUIRE(joystick != nullptr);
    auto raw_guid = SDL_GetJoystickGUID(joystick);
    raw_guid.data[2] = raw_guid.data[3] = 0;
    SDL_GUIDToString(raw_guid, guid, sizeof(guid));
    PadIdentifier pad{.guid = Common::UUID{guid}, .port = 0};
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, 2, true));
    REQUIRE(SDL_SetJoystickVirtualAxis(joystick, 2, 16384));
    driver.PumpEvents();
    REQUIRE(driver.GetButton(pad, 2));
    REQUIRE_FALSE(driver.GetButton(pad, 0));
    REQUIRE(driver.GetAxis(pad, 2) > 0.49f);
    REQUIRE(driver.GetAxis(pad, 0) == 0.0f);
    REQUIRE(SDL_SetJoystickVirtualButton(joystick, 2, false));
    driver.PumpEvents();
    REQUIRE_FALSE(driver.GetButton(pad, 2));
}
