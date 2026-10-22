// GLideN64 behind ultramodern's RendererContext. RT64 is driven the same way on PC (a mupen64plus GFX_INFO over the
// recompiled game's RDRAM), so the memory layout GLideN64 expects (32-bit words in host order) is already what the
// recompiled code writes.
//
// Adapted from DK64Recomp-NX (switch/render_gliden64.cpp, https://github.com/Trinalord777/DK64Recomp-NX, GPL-3.0).
//
// 60 fps: the game still runs at 30 fps, and each display list is processed twice. The first pass draws the real frame;
// the second draws it again with every matrix halfway between the previous frame and this one, using the RT64 matrix
// groups the game's patches emit (gliden64_rt64ex.cpp). The in-between image is presented one VI before the real one.
// A frame whose passes wouldn't fit in the game's frame time, or a camera cut, is shown without an in-between image.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <chrono>
#include <thread>

#include <SDL.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_plugin.h"

#include "librecomp/addresses.hpp"
#include "mm64nx_profile.h"
#include "zelda_render.h"

void mm64nx_seed_gliden64_config();  // core_nx.cpp
// Frame interpolation, gliden64_rt64ex.cpp. dk64nx_interp_frame_begin resets the per-frame bookkeeping of the RT64
// extended GBI hooks and must run before every display list, interpolated or not.
extern "C" void dk64nx_interp_frame_begin();
extern "C" void dk64nx_rt64ex_save_state();
extern "C" void dk64nx_rt64ex_restore_state();
extern "C" bool dk64nx_interp_begin_second_pass(uint32_t first_pass_us, bool wanted);
extern "C" void dk64nx_interp_end_second_pass();
extern "C" void dk64nx_interp_pass2_done(uint32_t us);
extern "C" bool dk64nx_interp_present_in_between(uint32_t origin);
extern "C" void dk64nx_interp_present_done();
// Since the start: frames, frames with an in-between image, camera cuts, matrices left unmatched in second passes.
extern "C" void dk64nx_interp_totals(uint64_t out[4]);
extern "C" void mm64nx_debug_shown_buffer(uint32_t origin);
extern "C" void dk64nx_gliden64_save_shaders();
bool dk64nx_front_buffer_redrawn(uint32_t origin);

// Whether to draw in-between images (60 fps) or only the game's own frames (30 fps); set by nx_controls.cpp.
std::atomic<bool> mm64nx_interpolate{false};

int mm64nx_profile::pass = 0;
uint64_t mm64nx_profile::exclusive_ns[3][mm64nx_profile::Count]{};
mm64nx_profile::Scope* mm64nx_profile::Scope::top = nullptr;

// The game's memory, for the desktop build's debugging tools (desktop_platform.cpp).
uint8_t* mm64nx_rdram = nullptr;

// Watchdog: the graphics thread names the step it's in, and a separate thread logs it if no step changes for a few
// seconds, to find where a freeze happens.
static std::atomic<const char*> graphics_step{"starting"};
static std::atomic<uint64_t> graphics_steps{0};
static std::atomic<uint64_t> display_lists_received{0};

static void set_graphics_step(const char* step) {
    graphics_step = step;
    graphics_steps++;
}

static void watchdog_thread() {
    constexpr int stall_seconds = 3;
    uint64_t last_steps = graphics_steps;
    int stalled_seconds = 0;
    uint64_t last_display_lists = display_lists_received;
    int seconds_without_display_lists = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds{ 1 });

        // The game itself stopping: the screen keeps refreshing, but no new frames arrive.
        const uint64_t display_lists = display_lists_received;
        if (display_lists != last_display_lists) {
            if (seconds_without_display_lists >= stall_seconds) {
                printf("[Watchdog] the game sent frames again after %d s\n", seconds_without_display_lists);
            }
            last_display_lists = display_lists;
            seconds_without_display_lists = 0;
        }
        else if (++seconds_without_display_lists == stall_seconds ||
                 (seconds_without_display_lists > stall_seconds && seconds_without_display_lists % 10 == 0)) {
            printf("[Watchdog] no frame from the game for %d s; game mode %u, player state %u\n",
                seconds_without_display_lists,
                mm64nx_rdram ? mm64nx_rdram[(0x800D62C0 & 0x1FFFFFFF) ^ 3] : 0,
                mm64nx_rdram ? mm64nx_rdram[(0x80135ED8 & 0x1FFFFFFF) ^ 3] : 0);
        }

        const uint64_t steps = graphics_steps;
        if (steps != last_steps) {
            if (stalled_seconds >= stall_seconds) {
                printf("[Watchdog] graphics thread resumed after %d s\n", stalled_seconds);
            }
            last_steps = steps;
            stalled_seconds = 0;
            continue;
        }
        stalled_seconds++;
        if (stalled_seconds == stall_seconds || (stalled_seconds > stall_seconds && stalled_seconds % 10 == 0)) {
            printf("[Watchdog] graphics thread stuck for %d s in: %s\n", stalled_seconds, graphics_step.load());
        }
    }
}

// Used by gliden64_rt64ex.cpp. Presenting a due screen update between the two passes needs runtime support that
// isn't ported, so it's off.
std::atomic<bool> dk64nx_mid_present{false};

static uint64_t ticks_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

extern "C" unsigned long long dk64nx_ms_since_boot() {
    return ticks_us() / 1000;
}

namespace {
    // OSTask is copied where the RSP boot code leaves it, at the end of DMEM.
    constexpr uint32_t TASK_DMEM_OFFSET = 0xFC0;
    constexpr uint32_t KSEG_MASK = 0x1FFFFFFF;
    // libultra's ROM header: GLideN64 reads the internal name from it for its per-game tweaks.
    constexpr char ROM_NAME[] = "MEGA MAN 64         ";

    uint8_t dmem[0x1000];
    uint8_t imem[0x1000];
    uint8_t rom_header[0x40];
    unsigned int mi_intr_reg;
    unsigned int sp_status_reg;
    unsigned int dpc_start_reg, dpc_end_reg, dpc_current_reg, dpc_status_reg;
    unsigned int dpc_clock_reg, dpc_bufbusy_reg, dpc_pipebusy_reg, dpc_tmem_reg;
    const unsigned int rdram_size = recomp::mem_size;

    // ultramodern signals SP/DP completion itself once send_dl returns.
    void check_interrupts() {}

    void debug_callback(void*, int level, const char* message) {
        if (level <= M64MSG_WARNING) {
            fprintf(stderr, "[GLideN64] %s\n", message);
        }
    }

    // GLideN64 reads the header as 32-bit host-order words, like the ROM image mupen64plus keeps.
    void build_rom_header() {
        memset(rom_header, 0, sizeof(rom_header));
        for (size_t i = 0; i < sizeof(ROM_NAME) - 1; i++) {
            rom_header[(0x20 + i) ^ 3] = static_cast<uint8_t>(ROM_NAME[i]);
        }
    }

    uint32_t to_physical(uint32_t address) {
        return address & KSEG_MASK;
    }

    uint32_t microseconds_since(uint64_t start_us) {
        return static_cast<uint32_t>(ticks_us() - start_us);
    }

    uint64_t present_ns = 0;  // time spent presenting (UpdateScreen), since the last report

    // Logs every 10 seconds how many frames got an in-between image, how long the passes took, and how each pass's
    // time divides between GLideN64's stages (mm64nx_profile.h), in milliseconds per frame.
    void report_interpolation(uint32_t first_pass_us, uint32_t second_pass_us) {
        static uint64_t last_report = ticks_us();
        static uint64_t last_totals[4]{};
        static uint64_t first_pass_total_us = 0, second_pass_total_us = 0, second_passes = 0, frames = 0;
        first_pass_total_us += first_pass_us;
        frames++;
        if (second_pass_us != 0) {
            second_pass_total_us += second_pass_us;
            second_passes++;
        }
        if (ticks_us() - last_report < 10'000'000ull) {
            return;
        }
        uint64_t totals[4];
        dk64nx_interp_totals(totals);
        printf("[GLideN64] %llu frames, %llu interpolated, %llu camera cuts; pass 1 %.1f ms, pass 2 %.1f ms, present %.1f ms\n",
            (unsigned long long)(totals[0] - last_totals[0]), (unsigned long long)(totals[1] - last_totals[1]),
            (unsigned long long)(totals[2] - last_totals[2]), first_pass_total_us / 1000.0 / frames,
            second_passes == 0 ? 0.0 : second_pass_total_us / 1000.0 / second_passes, present_ns / 1e6 / frames);

#ifdef MM64NX_PROFILING
        static const char* names[mm64nx_profile::Count] = { "vertex", "tris", "rects", "states", "textures", "combiner", "loads" };
        const uint64_t pass_counts[2] = { frames, second_passes };
        const uint64_t pass_totals_us[2] = { first_pass_total_us, second_pass_total_us };
        for (int pass = 0; pass < 2; pass++) {
            if (pass_counts[pass] == 0) {
                continue;
            }
            char line[256];
            int length = snprintf(line, sizeof(line), "[Profile] pass %d:", pass + 1);
            uint64_t staged_ns = 0;
            for (int c = 0; c < mm64nx_profile::Count; c++) {
                staged_ns += mm64nx_profile::exclusive_ns[pass][c];
                length += snprintf(line + length, sizeof(line) - length, " %s %.2f", names[c],
                    mm64nx_profile::exclusive_ns[pass][c] / 1e6 / pass_counts[pass]);
            }
            const double other_ms = (pass_totals_us[pass] * 1000.0 - staged_ns) / 1e6 / pass_counts[pass];
            snprintf(line + length, sizeof(line) - length, " other %.2f", other_ms);
            printf("%s\n", line);
        }
        std::memset(mm64nx_profile::exclusive_ns, 0, sizeof(mm64nx_profile::exclusive_ns));
#endif

        std::memcpy(last_totals, totals, sizeof(totals));
        present_ns = 0;
        first_pass_total_us = second_pass_total_us = second_passes = frames = 0;
        last_report = ticks_us();
    }

    // Saves new shaders to the SD card at most every 10 s, so that the next start compiles them before play. Called on
    // the graphics thread between display lists: with threaded video, saving from inside a buffer swap (which runs on
    // GLideN64's GL thread) would queue GL commands to that same thread and wait for them forever.
    void save_shaders_periodically() {
        constexpr uint64_t save_interval_us = 10'000'000;
        static uint64_t saved_at = ticks_us();
        if (ticks_us() - saved_at >= save_interval_us) {
            saved_at = ticks_us();
            dk64nx_gliden64_save_shaders();
        }
    }

    // Presents through GLideN64 while timing it.
    void present() {
        mm64nx_profile::pass = 2;
        const uint64_t start = mm64nx_profile::now_ns();
        set_graphics_step("presenting (UpdateScreen)");
        UpdateScreen();
        set_graphics_step("idle (waiting for work)");
        present_ns += mm64nx_profile::now_ns() - start;
    }

    class GLideN64RendererContext final : public ultramodern::renderer::RendererContext {
    public:
        explicit GLideN64RendererContext(uint8_t* rdram) {
            chosen_api = ultramodern::renderer::GraphicsApi::Auto;
            mm64nx_seed_gliden64_config();
            build_rom_header();
            mm64nx_rdram = rdram;

            if (PluginStartup(nullptr, nullptr, debug_callback) != M64ERR_SUCCESS) {
                setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
                return;
            }

            auto* vi = ultramodern::renderer::get_vi_regs();
            GFX_INFO info{};
            info.HEADER = rom_header;
            info.RDRAM = rdram;
            info.DMEM = dmem;
            info.IMEM = imem;
            info.MI_INTR_REG = &mi_intr_reg;
            info.DPC_START_REG = &dpc_start_reg;
            info.DPC_END_REG = &dpc_end_reg;
            info.DPC_CURRENT_REG = &dpc_current_reg;
            info.DPC_STATUS_REG = &dpc_status_reg;
            info.DPC_CLOCK_REG = &dpc_clock_reg;
            info.DPC_BUFBUSY_REG = &dpc_bufbusy_reg;
            info.DPC_PIPEBUSY_REG = &dpc_pipebusy_reg;
            info.DPC_TMEM_REG = &dpc_tmem_reg;
            info.VI_STATUS_REG = &vi->VI_STATUS_REG;
            info.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
            info.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
            info.VI_INTR_REG = &vi->VI_INTR_REG;
            info.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
            info.VI_TIMING_REG = &vi->VI_TIMING_REG;
            info.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
            info.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
            info.VI_LEAP_REG = &vi->VI_LEAP_REG;
            info.VI_H_START_REG = &vi->VI_H_START_REG;
            info.VI_V_START_REG = &vi->VI_V_START_REG;
            info.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
            info.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
            info.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;
            info.CheckInterrupts = check_interrupts;
            info.version = 2;
            info.SP_STATUS_REG = &sp_status_reg;
            info.RDRAM_SIZE = &rdram_size;

            if (!InitiateGFX(info) || !RomOpen()) {
                setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
                return;
            }
            setup_result = ultramodern::renderer::SetupResult::Success;
            opened = true;
            printf("[GLideN64] Renderer ready\n");
            std::thread{ watchdog_thread }.detach();
        }

        ~GLideN64RendererContext() override {
            shutdown();
        }

        bool valid() override { return opened; }

        bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override {
            return true;
        }

        void enable_instant_present() override {}

        void send_dl(const OSTask* task) override {
            // Lay the task out in DMEM as osSpTaskLoad does, with physical addresses.
            const OSTask_s& t = task->t;
            const uint32_t words[16] = {
                t.type, t.flags,
                to_physical(t.ucode_boot), t.ucode_boot_size,
                to_physical(t.ucode), t.ucode_size,
                to_physical(t.ucode_data), t.ucode_data_size,
                to_physical(t.dram_stack), t.dram_stack_size,
                to_physical(t.output_buff), t.output_buff_size,
                to_physical(t.data_ptr), t.data_size,
                to_physical(t.yield_data_ptr), t.yield_data_size,
            };
            memcpy(&dmem[TASK_DMEM_OFFSET], words, sizeof(words));
            // HALT | BROKE | TASKDONE, set before ProcessDList as GFX_INFO v2 requires.
            sp_status_reg = 0x0203;
            display_lists_received++;
            dk64nx_interp_frame_begin();
            dk64nx_rt64ex_save_state();
            const uint64_t first_start = ticks_us();
            mm64nx_profile::pass = 0;
            set_graphics_step("drawing (pass 1)");
            ProcessDList();
            const uint32_t first_pass_us = microseconds_since(first_start);

            uint32_t second_pass_us = 0;
            if (mm64nx_interpolate && dk64nx_interp_begin_second_pass(first_pass_us, true)) {
                const uint64_t second_start = ticks_us();
                dk64nx_rt64ex_restore_state();
                sp_status_reg = 0x0203;
                mm64nx_profile::pass = 1;
                set_graphics_step("drawing (pass 2, in-between image)");
                ProcessDList();
                dk64nx_interp_end_second_pass();
                second_pass_us = microseconds_since(second_start);
                dk64nx_interp_pass2_done(second_pass_us);
            }
            report_interpolation(first_pass_us, second_pass_us);
            set_graphics_step("saving shaders");
            save_shaders_periodically();
            set_graphics_step("idle (waiting for work)");
        }

        // For debugging: MM64NX_VI_LOG logs the VI registers whenever they change (besides the origin).
        void log_vi_changes() {
            static const bool enabled = getenv("MM64NX_VI_LOG") != nullptr;
            if (!enabled) {
                return;
            }
            static ultramodern::renderer::ViRegs last{};
            ultramodern::renderer::ViRegs cur = *ultramodern::renderer::get_vi_regs();
            cur.VI_ORIGIN_REG = 0;
            if (std::memcmp(&cur, &last, sizeof(cur)) != 0) {
                printf("[VI] status %08x width %u xscale %08x yscale %08x hstart %08x vstart %08x vsync %u\n",
                    cur.VI_STATUS_REG, cur.VI_WIDTH_REG, cur.VI_X_SCALE_REG, cur.VI_Y_SCALE_REG, cur.VI_H_START_REG,
                    cur.VI_V_START_REG, cur.VI_V_SYNC_REG);
                last = cur;
            }
        }

        void update_screen() override {
            log_vi_changes();
            const uint32_t origin = ultramodern::renderer::get_vi_regs()->VI_ORIGIN_REG;
            static const bool log_buffers = getenv("MM64NX_VI_LOG") != nullptr;
            if (log_buffers) {
                mm64nx_debug_shown_buffer(origin);
            }
            // A new frame shows its in-between image first; the next VI presents the real one.
            if (dk64nx_interp_present_in_between(origin)) {
                present();
                dk64nx_interp_present_done();
                held_presents = 0;
                presented_origin = origin;
                return;
            }
            // Without an RDP freeze, the game can start its next frame in the buffer that's still on screen, and
            // re-presenting it would show that frame early. Keep the image on screen instead, for a few VIs at most so
            // that a single-buffered scene still refreshes.
            if (origin == presented_origin && held_presents < max_held_presents && dk64nx_front_buffer_redrawn(origin)) {
                held_presents++;
                return;
            }
            held_presents = 0;
            presented_origin = origin;
            present();
        }

        void shutdown() override {
            if (opened) {
                RomClosed();
                PluginShutdown();
                opened = false;
            }
        }

        uint32_t get_display_framerate() const override { return 60; }
        float get_resolution_scale() const override { return 3.0f; }

    private:
        static constexpr uint32_t max_held_presents = 3;

        bool opened = false;
        uint32_t presented_origin = 0;
        uint32_t held_presents = 0;
    };
}

std::unique_ptr<ultramodern::renderer::RendererContext> zelda64::renderer::create_render_context(
    uint8_t* rdram, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<GLideN64RendererContext>(rdram);
}

// Settings and texture packs that only exist with RT64.
RT64::UserConfiguration::Antialiasing zelda64::renderer::RT64MaxMSAA() {
    return RT64::UserConfiguration::Antialiasing::None;
}

bool zelda64::renderer::RT64SamplePositionsSupported() {
    return false;
}

bool zelda64::renderer::RT64HighPrecisionFBEnabled() {
    return false;
}

void zelda64::renderer::trigger_texture_pack_update() {}

void zelda64::renderer::enable_texture_pack(const recomp::mods::ModContext&, const recomp::mods::ModHandle&) {}

void zelda64::renderer::disable_texture_pack(const recomp::mods::ModHandle&) {}

void zelda64::renderer::secondary_enable_texture_pack(const std::string&) {}

void zelda64::renderer::secondary_disable_texture_pack(const std::string&) {}

bool zelda64::renderer::is_texture_pack_enable_config_option(const recomp::mods::ConfigOption&, bool) {
    return false;
}
