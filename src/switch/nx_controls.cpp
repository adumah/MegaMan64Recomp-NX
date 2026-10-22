// The Switch's controller layout and frame rate setting. There's no config menu yet, so the PlayStation-style layout
// in src/game/input.cpp (switch_psx_controller_mappings) is applied at every start instead of the bindings in
// controls.json, and the frame rate is switched with a button combination.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "SDL.h"

#include "nx_platform.h"
#include "recomp_input.h"

void assign_all_mappings(recomp::InputDevice device, const recomp::DefaultN64Mappings& values);  // src/game/config.cpp
extern std::atomic<bool> mm64nx_interpolate;  // src/switch/gliden64/render_gliden64.cpp

extern uint8_t* mm64nx_rdram;  // src/switch/gliden64/render_gliden64.cpp

namespace {
    // Game mode, found by comparing memory snapshots (desktop build, MM64NX_RAM_DUMP_MS) in the ruins and Cardon
    // Forest: 3 in the field, 4 in the pause menu, 0 on the title screen and in cutscenes. The view only turns in the
    // field.
    constexpr uint32_t game_mode = 0x800D62C0;
    constexpr uint8_t game_mode_field = 3;
    // Mega Man's state: 1 standing, other small values while moving or acting. 255 was last seen before the game
    // stopped once while Mega Man stood idle; the view isn't turned in that state, in case writing his heading then
    // is what stalled it.
    constexpr uint32_t player_state = 0x80135ED8;
    constexpr uint8_t player_state_blocked = 255;
    // Mega Man's facing in 4096ths of a turn. The camera stays behind it.
    constexpr uint32_t player_heading = 0x80204A06;

    constexpr float turn_deadzone = 0.15f;
    // Full stick: a whole turn in 1.5 s.
    constexpr float full_turn_per_second = 4096.0f / 1.5f;
    // Finer control near the centre.
    constexpr float turn_curve = 1.5f;
    // Quick turn (stick pushed down): half a turn in this time. The stick has to come back above the release point
    // before it turns again.
    constexpr float quick_turn_seconds = 0.25f;
    constexpr float quick_turn_threshold = 0.7f;
    constexpr float quick_turn_release = 0.4f;

    // RDRAM holds 32-bit words in host byte order.
    uint8_t& rdram_u8(uint32_t address) {
        return mm64nx_rdram[(address & 0x1FFFFFFF) ^ 3];
    }

    uint16_t& rdram_u16(uint32_t address) {
        return *reinterpret_cast<uint16_t*>(&mm64nx_rdram[(address & 0x1FFFFFFF) ^ 2]);
    }
}

void mm64nx::turn_view(float stick_x, float stick_y) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last_call = clock::now();
    static float pending_turn = 0.0f;
    static float quick_turn_remaining = 0.0f;
    static bool stick_was_down = false;

    clock::time_point now = clock::now();
    float seconds = std::min(std::chrono::duration<float>(now - last_call).count(), 0.05f);
    last_call = now;

    // Pushing the stick down turns Mega Man around, once per push.
    bool stick_down = stick_y > (stick_was_down ? quick_turn_release : quick_turn_threshold);
    bool quick_turn_pressed = stick_down && !stick_was_down;
    stick_was_down = stick_down;

    if (mm64nx_rdram == nullptr || rdram_u8(game_mode) != game_mode_field || rdram_u8(player_state) == player_state_blocked) {
        pending_turn = 0.0f;
        quick_turn_remaining = 0.0f;
        return;
    }

    if (quick_turn_pressed && quick_turn_remaining <= 0.0f) {
        quick_turn_remaining = 2048.0f;
    }
    if (quick_turn_remaining > 0.0f) {
        float step = std::min(quick_turn_remaining, 2048.0f / quick_turn_seconds * seconds);
        pending_turn += step;
        quick_turn_remaining -= step;
    }

    float amount = std::fabs(stick_x);
    if (amount > turn_deadzone && !stick_down) {
        float speed = std::pow(std::min((amount - turn_deadzone) / (1.0f - turn_deadzone), 1.0f), turn_curve);
        pending_turn += std::copysign(speed * full_turn_per_second * seconds, stick_x);
    }

    // Whole units only; the rest carries over so that slow turns still move.
    int units = static_cast<int>(pending_turn);
    pending_turn -= units;
    if (units != 0) {
        uint16_t& heading = rdram_u16(player_heading);
        // The heading grows when turning right.
        heading = static_cast<uint16_t>((heading + units) & 0xFFF);
    }
}

void mm64nx::apply_controller_layout() {
    assign_all_mappings(recomp::InputDevice::Controller, recomp::switch_psx_controller_mappings);
}

namespace {
    std::filesystem::path frame_rate_path() {
        return mm64nx::get_app_folder_path() / "framerate.txt";
    }

    void set_interpolation(bool enabled) {
        mm64nx_interpolate = enabled;
        printf("[Switch] Frame rate: %d fps\n", enabled ? 60 : 30);
    }

    // One short rumble for 30 fps, two for 60.
    void rumble_feedback(SDL_GameController* controller, bool interpolating) {
        int pulses = interpolating ? 2 : 1;
        for (int i = 0; i < pulses; i++) {
            SDL_GameControllerRumble(controller, 0xC000, 0xC000, 120);
            if (i + 1 < pulses) {
                SDL_Delay(200);
            }
        }
    }
}

// 30 fps unless 60 was chosen: the game's own rate needs no more than the console's standard clocks.
void mm64nx::load_frame_rate() {
    std::string value;
    std::ifstream{ frame_rate_path() } >> value;
    set_interpolation(value == "60");
}

// L3 + R3 (both sticks pressed) switches between 30 and 60 fps.
void mm64nx::check_frame_rate_buttons() {
    static bool was_held = false;
    SDL_GameController* pressed_on = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (controller != nullptr &&
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK) &&
            SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) {
            pressed_on = controller;
        }
    }

    bool held = pressed_on != nullptr;
    if (held && !was_held) {
        bool interpolating = !mm64nx_interpolate;
        set_interpolation(interpolating);
        std::ofstream{ frame_rate_path() } << (interpolating ? "60" : "30") << "\n";
        rumble_feedback(pressed_on, interpolating);
    }
    was_held = held;
}
