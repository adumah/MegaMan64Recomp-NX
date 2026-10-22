// Desktop counterparts of nx_platform.cpp, for building the GLideN64 renderer on a PC (MM64NX_DESKTOP). The desktop
// build renders exactly like the Switch one, which makes it possible to debug graphics and reverse engineer the game
// without a console.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "nx_platform.h"

std::filesystem::path mm64nx::get_app_folder_path() {
    // MM64NX_DATA_DIR overrides the default folder next to the working directory.
    if (const char* dir = std::getenv("MM64NX_DATA_DIR")) {
        return std::filesystem::path{ dir };
    }
    return std::filesystem::current_path() / "mm64nx-data";
}

bool mm64nx::is_applet_mode() {
    return false;
}


namespace {
    uint64_t ms_since_start() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return duration_cast<milliseconds>(steady_clock::now() - start).count();
    }

    struct ScriptedPress {
        uint64_t start_ms;
        uint64_t end_ms;
        uint16_t buttons;
        float right_x;
    };

    // N64 button bits, as in the DEFINE_N64_BUTTON_INPUTS table of include/recomp_input.h.
    uint16_t button_from_name(const std::string& name) {
        static const std::pair<const char*, uint16_t> names[] = {
            {"A", 0x8000}, {"B", 0x4000}, {"Z", 0x2000}, {"START", 0x1000},
            {"DU", 0x0800}, {"DD", 0x0400}, {"DL", 0x0200}, {"DR", 0x0100},
            {"L", 0x0020}, {"R", 0x0010}, {"CU", 0x0008}, {"CD", 0x0004}, {"CL", 0x0002}, {"CR", 0x0001},
        };
        for (const auto& [n, bits] : names) {
            if (name == n) {
                return bits;
            }
        }
        fprintf(stderr, "[Desktop] Unknown button in input script: %s\n", name.c_str());
        return 0;
    }

    // Lines of "start_ms end_ms BUTTON [BUTTON...]", times since the program started; # starts a comment.
    const std::vector<ScriptedPress>& script() {
        static const std::vector<ScriptedPress> presses = [] {
            std::vector<ScriptedPress> out;
            const char* path = std::getenv("MM64NX_INPUT_SCRIPT");
            if (path == nullptr) {
                return out;
            }
            std::ifstream file{ path };
            std::string line;
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#') {
                    continue;
                }
                std::istringstream fields{ line };
                ScriptedPress press{};
                fields >> press.start_ms >> press.end_ms;
                std::string name;
                while (fields >> name) {
                    if (name.rfind("RX", 0) == 0) {
                        press.right_x = std::strtof(name.c_str() + 2, nullptr) / 100.0f;
                    }
                    else {
                        press.buttons |= button_from_name(name);
                    }
                }
                out.push_back(press);
            }
            printf("[Desktop] %zu scripted presses\n", out.size());
            return out;
        }();
        return presses;
    }
}

extern std::atomic<bool> mm64nx_screenshot_requested;  // src/switch/gliden64/core_nx.cpp
extern uint8_t* mm64nx_rdram;  // src/switch/gliden64/render_gliden64.cpp

// MM64NX_RAM_DUMP_MS="t1,t2,...": writes the game's 8 MB of RDRAM to ram_<t>.bin at those times (ms since start), for
// finding game state by comparing snapshots.
static void dump_ram_on_schedule() {
    static std::vector<uint64_t> times = [] {
        std::vector<uint64_t> out;
        if (const char* list = std::getenv("MM64NX_RAM_DUMP_MS")) {
            std::istringstream fields{ list };
            std::string field;
            while (std::getline(fields, field, ',')) {
                out.push_back(std::strtoull(field.c_str(), nullptr, 10));
            }
        }
        return out;
    }();
    static size_t next = 0;
    if (next < times.size() && mm64nx_rdram != nullptr && ms_since_start() >= times[next]) {
        std::ofstream{ "ram_" + std::to_string(times[next]) + ".bin", std::ios::binary }
            .write(reinterpret_cast<const char*>(mm64nx_rdram), 8 * 1024 * 1024);
        next++;
    }
}

void mm64nx::debug_tick() {
    dump_ram_on_schedule();
    static const uint64_t shot_every_ms = [] {
        const char* value = std::getenv("MM64NX_SHOT_EVERY_MS");
        return value == nullptr ? 0ull : std::strtoull(value, nullptr, 10);
    }();
    static uint64_t next_shot_ms = shot_every_ms;
    if (shot_every_ms != 0 && ms_since_start() >= next_shot_ms) {
        mm64nx_screenshot_requested = true;
        next_shot_ms += shot_every_ms;
    }
}

uint16_t mm64nx::scripted_buttons() {
    const uint64_t now = ms_since_start();
    uint16_t buttons = 0;
    for (const ScriptedPress& press : script()) {
        if (now >= press.start_ms && now < press.end_ms) {
            buttons |= press.buttons;
        }
    }
    return buttons;
}

float mm64nx::scripted_right_x() {
    const uint64_t now = ms_since_start();
    for (const ScriptedPress& press : script()) {
        if (now >= press.start_ms && now < press.end_ms && press.right_x != 0.0f) {
            return press.right_x;
        }
    }
    return 0.0f;
}
