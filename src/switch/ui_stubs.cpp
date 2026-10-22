// Stand-ins for the parts of src/ui that the game code depends on, for the Switch build, which has no RmlUi menus.
// Settings that are normally held by the config menu are stored here directly (and are still loaded from and saved
// to the config files by src/game/config.cpp), and UI requests are logged instead of shown.
// This file goes away once the RmlUi launcher and config menu are ported.

#include <atomic>
#include <cstdio>

#include "recomp_ui.h"
#include "recomp_input.h"
#include "zelda_config.h"
#include "zelda_sound.h"
#include "zelda_debug.h"
#include "ultramodern/ultramodern.hpp"

namespace {
    struct ControlOptions {
        int rumble_strength = 50;
        int gyro_sensitivity = 50;
        int mouse_sensitivity = 0;
        int joystick_deadzone = 5;
        zelda64::TargetingMode targeting_mode{};
        recomp::BackgroundInputMode background_input_mode{};
        zelda64::RadioBoxMode radio_comm_box_mode{};
        zelda64::AimInvertMode analog_camera_invert_mode{};
    } control_options;

    struct SoundOptions {
        std::atomic<int> main_volume = 100;
        std::atomic<int> bgm_volume = 100;
        std::atomic<int> low_health_beeps_enabled = 1;
    } sound_options;

    bool debug_mode_enabled = false;
}

int recomp::get_rumble_strength() { return control_options.rumble_strength; }
void recomp::set_rumble_strength(int strength) { control_options.rumble_strength = strength; }
int recomp::get_gyro_sensitivity() { return control_options.gyro_sensitivity; }
void recomp::set_gyro_sensitivity(int sensitivity) { control_options.gyro_sensitivity = sensitivity; }
int recomp::get_mouse_sensitivity() { return control_options.mouse_sensitivity; }
void recomp::set_mouse_sensitivity(int sensitivity) { control_options.mouse_sensitivity = sensitivity; }
int recomp::get_joystick_deadzone() { return control_options.joystick_deadzone; }
void recomp::set_joystick_deadzone(int deadzone) { control_options.joystick_deadzone = deadzone; }
recomp::BackgroundInputMode recomp::get_background_input_mode() { return control_options.background_input_mode; }
void recomp::set_background_input_mode(recomp::BackgroundInputMode mode) { control_options.background_input_mode = mode; }

// Input binding isn't possible without the config menu.
int recomp::get_scanned_input_index() { return -1; }
void recomp::cancel_scanning_input() {}

zelda64::TargetingMode zelda64::get_targeting_mode() { return control_options.targeting_mode; }
void zelda64::set_targeting_mode(zelda64::TargetingMode mode) { control_options.targeting_mode = mode; }
zelda64::RadioBoxMode zelda64::get_radio_comm_box_mode() { return control_options.radio_comm_box_mode; }
zelda64::AimInvertMode zelda64::get_analog_camera_invert_mode() { return control_options.analog_camera_invert_mode; }
zelda64::AimInvertMode zelda64::get_invert_y_axis_mode() { return zelda64::AimInvertMode::Off; }
void zelda64::set_analog_camera_invert_mode(zelda64::AimInvertMode mode) { control_options.analog_camera_invert_mode = mode; }

void zelda64::reset_sound_settings() {
    sound_options.main_volume = 100;
    sound_options.bgm_volume = 100;
    sound_options.low_health_beeps_enabled = 1;
}
int zelda64::get_main_volume() { return sound_options.main_volume.load(); }
void zelda64::set_main_volume(int volume) { sound_options.main_volume.store(volume); }
int zelda64::get_bgm_volume() { return sound_options.bgm_volume.load(); }
void zelda64::set_bgm_volume(int volume) { sound_options.bgm_volume.store(volume); }
bool zelda64::get_low_health_beeps_enabled() { return (bool)sound_options.low_health_beeps_enabled.load(); }
void zelda64::set_low_health_beeps_enabled(bool enabled) { sound_options.low_health_beeps_enabled.store((int)enabled); }

bool zelda64::get_debug_mode_enabled() { return debug_mode_enabled; }
void zelda64::set_debug_mode_enabled(bool enabled) { debug_mode_enabled = enabled; }

void zelda64::open_quit_game_prompt() {
    ultramodern::quit();
}

void recompui::message_box(const char* msg) {
    fprintf(stderr, "[Message] %s\n", msg);
}

void recompui::get_window_size(int& width, int& height) {
    width = 1280;
    height = 720;
}

bool recompui::is_context_capturing_input() { return false; }
void recompui::queue_event(const SDL_Event&) {}
void recompui::activate_mouse() {}
void recompui::drop_files(const std::list<std::filesystem::path>&) {}
void recompui::process_game_started() {}
void recompui::toggle_fullscreen() {}
void recompui::update_supported_options() {}
