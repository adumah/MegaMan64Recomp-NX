#ifndef __NX_PLATFORM_H__
#define __NX_PLATFORM_H__

#include <cstdint>
#include <filesystem>

namespace mm64nx {
    // Folder on the SD card that holds the ROM, saves and configuration.
    std::filesystem::path get_app_folder_path();

    // Whether the program was launched from an applet (e.g. the album) instead of through title override.
    // Applets only get a few hundred megabytes of memory, which isn't enough to run the game.
    bool is_applet_mode();

    // Frame rate (src/switch/nx_controls.cpp): the game's own 30 fps, or 60 with interpolated in-between images.
    // load_frame_rate applies the saved choice; check_frame_rate_buttons switches it on L3 + R3.
    void load_frame_rate();
    void check_frame_rate_buttons();

    // Developer tools of the desktop build (src/switch/desktop_platform.cpp); no-ops on the Switch.
    // debug_tick runs on the main thread's loop: timed screenshots (MM64NX_SHOT_EVERY_MS).
    // scripted_buttons returns N64 buttons to press now, from the MM64NX_INPUT_SCRIPT file.
    void debug_tick();
    uint16_t scripted_buttons();
    // Right stick X (-1..1) from the input script's RX<percent> entries.
    float scripted_right_x();

    // Turns Mega Man, and the camera behind him, with the right stick (-1..1, y positive down) while the player has
    // control: X turns, down turns him around (src/switch/nx_controls.cpp). Called from the game's controller reads.
    void turn_view(float stick_x, float stick_y);

    // Applies the Switch's PlayStation-style controller layout (src/switch/nx_controls.cpp).
    void apply_controller_layout();
}

#endif
