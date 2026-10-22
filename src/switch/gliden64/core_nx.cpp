// Minimal mupen64plus "core" for GLideN64: the plugin looks up its config store and video
// extension (VidExt) by name at PluginStartup; dlsym() below answers from a static table.
// Config lives in memory, seeded with Switch-friendly values before GLideN64 sets its defaults.
//
// Adapted from DK64Recomp-NX (switch/core_switch.cpp, https://github.com/Trinalord777/DK64Recomp-NX, GPL-3.0).
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

#include "m64p_config.h"
#include "m64p_types.h"
#include "m64p_vidext.h"
#include "nx_platform.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

extern SDL_Window* window;  // src/main/main.cpp

// Set by the screenshot button combination (src/main/main.cpp); the next presented frame is saved as a PNG.
std::atomic<bool> mm64nx_screenshot_requested{false};

namespace {
    const std::string& data_dir() {
        static const std::string dir = mm64nx::get_app_folder_path().string();
        return dir;
    }

    constexpr int screen_width = 1280;
    constexpr int screen_height = 720;
    // Internal resolution as a multiple of the N64's: 3x (960x720) fills the height of the 720p screen.
    constexpr int resolution_factor = 3;

    struct Param {
        m64p_type type = M64TYPE_INT;
        int int_value = 0;
        float float_value = 0.0f;
        std::string string_value;
    };
    using Section = std::map<std::string, Param>;

    std::map<std::string, Section>& sections() {
        static std::map<std::string, Section> store;
        return store;
    }

    Section* as_section(m64p_handle handle) {
        return static_cast<Section*>(handle);
    }

    const Param* find_param(m64p_handle handle, const char* name) {
        Section* section = as_section(handle);
        auto it = section->find(name);
        return it == section->end() ? nullptr : &it->second;
    }

    void set_param(m64p_handle handle, const char* name, const Param& param, bool only_if_absent) {
        Section* section = as_section(handle);
        if (only_if_absent && section->count(name) != 0) {
            return;
        }
        (*section)[name] = param;
    }

    Param int_param(int value) { Param p; p.type = M64TYPE_INT; p.int_value = value; return p; }
    Param bool_param(bool value) { Param p; p.type = M64TYPE_BOOL; p.int_value = value ? 1 : 0; return p; }

    SDL_GLContext gl_context = nullptr;
    int swap_interval = 1;
}

extern "C" {

m64p_error nx_ConfigOpenSection(const char* name, m64p_handle* handle) {
    *handle = &sections()[name];
    return M64ERR_SUCCESS;
}

// Clearing (not erasing) keeps handles GLideN64 already holds valid.
m64p_error nx_ConfigDeleteSection(const char* name) {
    sections()[name].clear();
    return M64ERR_SUCCESS;
}

m64p_error nx_ConfigSaveSection(const char*) { return M64ERR_SUCCESS; }
m64p_error nx_ConfigSaveFile(void) { return M64ERR_SUCCESS; }

m64p_error nx_ConfigSetDefaultInt(m64p_handle handle, const char* name, int value, const char*) {
    set_param(handle, name, int_param(value), true);
    return M64ERR_SUCCESS;
}

m64p_error nx_ConfigSetDefaultFloat(m64p_handle handle, const char* name, float value, const char*) {
    Param p;
    p.type = M64TYPE_FLOAT;
    p.float_value = value;
    set_param(handle, name, p, true);
    return M64ERR_SUCCESS;
}

m64p_error nx_ConfigSetDefaultBool(m64p_handle handle, const char* name, int value, const char*) {
    set_param(handle, name, bool_param(value != 0), true);
    return M64ERR_SUCCESS;
}

m64p_error nx_ConfigSetDefaultString(m64p_handle handle, const char* name, const char* value, const char*) {
    Param p;
    p.type = M64TYPE_STRING;
    p.string_value = value;
    set_param(handle, name, p, true);
    return M64ERR_SUCCESS;
}

int nx_ConfigGetParamInt(m64p_handle handle, const char* name) {
    const Param* p = find_param(handle, name);
    return p == nullptr ? 0 : (p->type == M64TYPE_FLOAT ? static_cast<int>(p->float_value) : p->int_value);
}

float nx_ConfigGetParamFloat(m64p_handle handle, const char* name) {
    const Param* p = find_param(handle, name);
    return p == nullptr ? 0.0f : (p->type == M64TYPE_FLOAT ? p->float_value : static_cast<float>(p->int_value));
}

int nx_ConfigGetParamBool(m64p_handle handle, const char* name) {
    return nx_ConfigGetParamInt(handle, name) != 0;
}

const char* nx_ConfigGetParamString(m64p_handle handle, const char* name) {
    const Param* p = find_param(handle, name);
    return p == nullptr ? "" : p->string_value.c_str();
}

m64p_error nx_ConfigSetParameter(m64p_handle handle, const char* name, m64p_type type, const void* value) {
    Param p;
    p.type = type;
    if (type == M64TYPE_FLOAT) p.float_value = *static_cast<const float*>(value);
    else if (type == M64TYPE_STRING) p.string_value = static_cast<const char*>(value);
    else p.int_value = *static_cast<const int*>(value);
    set_param(handle, name, p, false);
    return M64ERR_SUCCESS;
}

m64p_error nx_ConfigGetParameter(m64p_handle handle, const char* name, m64p_type type, void* value, int max_size) {
    const Param* p = find_param(handle, name);
    if (p == nullptr) return M64ERR_INPUT_NOT_FOUND;
    if (type == M64TYPE_FLOAT) *static_cast<float*>(value) = p->float_value;
    else if (type == M64TYPE_STRING) {
        strncpy(static_cast<char*>(value), p->string_value.c_str(), max_size - 1);
        static_cast<char*>(value)[max_size - 1] = '\0';
    }
    else *static_cast<int*>(value) = p->int_value;
    return M64ERR_SUCCESS;
}

const char* nx_ConfigGetParameterHelp(m64p_handle, const char*) { return ""; }

// No per-game GLideN64.custom.ini on Switch: the overrides below are DK64-specific already.
m64p_error nx_ConfigExternalOpen(const char*, m64p_handle*) { return M64ERR_FILES; }
m64p_error nx_ConfigExternalClose(m64p_handle) { return M64ERR_SUCCESS; }
m64p_error nx_ConfigExternalGetParameter(m64p_handle, const char*, const char*, char*, int) { return M64ERR_INPUT_NOT_FOUND; }

const char* nx_ConfigGetSharedDataFilepath(const char*) { return data_dir().c_str(); }
const char* nx_ConfigGetUserConfigPath(void) { return data_dir().c_str(); }
const char* nx_ConfigGetUserDataPath(void) { return data_dir().c_str(); }
const char* nx_ConfigGetUserCachePath(void) { return data_dir().c_str(); }

m64p_error nx_VidExt_Init(void) { return M64ERR_SUCCESS; }

m64p_error nx_VidExt_Quit(void) {
    if (gl_context != nullptr) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = nullptr;
    }
    return M64ERR_SUCCESS;
}

m64p_error nx_VidExt_ListFullscreenModes(m64p_2d_size* modes, int* count) {
    if (*count >= 1) {
        modes[0].uiWidth = screen_width;
        modes[0].uiHeight = screen_height;
        *count = 1;
    }
    return M64ERR_SUCCESS;
}

m64p_error nx_VidExt_ListFullscreenRates(m64p_2d_size, int* count, int* rates) {
    if (*count >= 1) {
        rates[0] = 60;
        *count = 1;
    }
    return M64ERR_SUCCESS;
}

// Runs on ultramodern's gfx thread (RomOpen), which then owns the GL context.
m64p_error nx_VidExt_SetVideoMode(int, int, int, m64p_video_mode, m64p_video_flags) {
    if (gl_context == nullptr) {
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context == nullptr) {
            fprintf(stderr, "[DK64NX] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return M64ERR_SYSTEM_FAIL;
        }
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(swap_interval);
    return M64ERR_SUCCESS;
}

m64p_error nx_VidExt_SetVideoModeWithRate(int width, int height, int, int bpp, m64p_video_mode mode, m64p_video_flags flags) {
    return nx_VidExt_SetVideoMode(width, height, bpp, mode, flags);
}

m64p_error nx_VidExt_ResizeWindow(int, int) { return M64ERR_SUCCESS; }
m64p_error nx_VidExt_SetCaption(const char*) { return M64ERR_SUCCESS; }
m64p_error nx_VidExt_ToggleFullScreen(void) { return M64ERR_SUCCESS; }

m64p_function nx_VidExt_GL_GetProcAddress(const char* name) {
    return reinterpret_cast<m64p_function>(SDL_GL_GetProcAddress(name));
}

m64p_error nx_VidExt_GL_SetAttribute(m64p_GLattr attr, int value) {
    switch (attr) {
        case M64P_GL_DOUBLEBUFFER: SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, value); break;
        case M64P_GL_BUFFER_SIZE: SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, value); break;
        case M64P_GL_DEPTH_SIZE: SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, value); break;
        case M64P_GL_RED_SIZE: SDL_GL_SetAttribute(SDL_GL_RED_SIZE, value); break;
        case M64P_GL_GREEN_SIZE: SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, value); break;
        case M64P_GL_BLUE_SIZE: SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, value); break;
        case M64P_GL_ALPHA_SIZE: SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, value); break;
        case M64P_GL_SWAP_CONTROL: swap_interval = value; break;
        case M64P_GL_MULTISAMPLEBUFFERS: SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, value); break;
        case M64P_GL_MULTISAMPLESAMPLES: SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, value); break;
        case M64P_GL_CONTEXT_MAJOR_VERSION: SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, value); break;
        case M64P_GL_CONTEXT_MINOR_VERSION: SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, value); break;
        case M64P_GL_CONTEXT_PROFILE_MASK:
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                value == M64P_GL_CONTEXT_PROFILE_ES ? SDL_GL_CONTEXT_PROFILE_ES :
                value == M64P_GL_CONTEXT_PROFILE_COMPATIBILITY ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY :
                SDL_GL_CONTEXT_PROFILE_CORE);
            break;
        default: return M64ERR_INPUT_INVALID;
    }
    return M64ERR_SUCCESS;
}

m64p_error nx_VidExt_GL_GetAttribute(m64p_GLattr attr, int* value) {
    SDL_GLattr sdl_attr;
    switch (attr) {
        case M64P_GL_DOUBLEBUFFER: sdl_attr = SDL_GL_DOUBLEBUFFER; break;
        case M64P_GL_BUFFER_SIZE: sdl_attr = SDL_GL_BUFFER_SIZE; break;
        case M64P_GL_DEPTH_SIZE: sdl_attr = SDL_GL_DEPTH_SIZE; break;
        case M64P_GL_RED_SIZE: sdl_attr = SDL_GL_RED_SIZE; break;
        case M64P_GL_GREEN_SIZE: sdl_attr = SDL_GL_GREEN_SIZE; break;
        case M64P_GL_BLUE_SIZE: sdl_attr = SDL_GL_BLUE_SIZE; break;
        case M64P_GL_ALPHA_SIZE: sdl_attr = SDL_GL_ALPHA_SIZE; break;
        case M64P_GL_SWAP_CONTROL: *value = SDL_GL_GetSwapInterval(); return M64ERR_SUCCESS;
        case M64P_GL_MULTISAMPLEBUFFERS: sdl_attr = SDL_GL_MULTISAMPLEBUFFERS; break;
        case M64P_GL_MULTISAMPLESAMPLES: sdl_attr = SDL_GL_MULTISAMPLESAMPLES; break;
        case M64P_GL_CONTEXT_MAJOR_VERSION: sdl_attr = SDL_GL_CONTEXT_MAJOR_VERSION; break;
        case M64P_GL_CONTEXT_MINOR_VERSION: sdl_attr = SDL_GL_CONTEXT_MINOR_VERSION; break;
        default: return M64ERR_INPUT_INVALID;
    }
    return SDL_GL_GetAttribute(sdl_attr, value) == 0 ? M64ERR_SUCCESS : M64ERR_SYSTEM_FAIL;
}

uint32_t mm64nx_display_list_count();  // src/switch/gliden64/gliden64_rt64ex.cpp

// Saves the image about to be presented to the app folder's screenshots directory. The Album isn't always available
// to homebrew, and this gives the exact frame GLideN64 drew.
static void save_screenshot() {
    using GetIntegerv = void (*)(unsigned int, int*);
    using BindFramebuffer = void (*)(unsigned int, unsigned int);
    using ReadPixels = void (*)(int, int, int, int, unsigned int, unsigned int, void*);
    constexpr unsigned int GL_READ_FRAMEBUFFER = 0x8CA8;
    constexpr unsigned int GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
    constexpr unsigned int GL_RGBA = 0x1908;
    constexpr unsigned int GL_UNSIGNED_BYTE = 0x1401;

    auto get_integerv = reinterpret_cast<GetIntegerv>(SDL_GL_GetProcAddress("glGetIntegerv"));
    auto bind_framebuffer = reinterpret_cast<BindFramebuffer>(SDL_GL_GetProcAddress("glBindFramebuffer"));
    auto read_pixels = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
    if (get_integerv == nullptr || bind_framebuffer == nullptr || read_pixels == nullptr) {
        return;
    }

    // GLideN64 caches its framebuffer bindings, so put back whatever it had bound.
    int previous_read_framebuffer = 0;
    get_integerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_framebuffer);
    bind_framebuffer(GL_READ_FRAMEBUFFER, 0);
    std::vector<uint8_t> pixels(screen_width * screen_height * 4);
    read_pixels(0, 0, screen_width, screen_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    bind_framebuffer(GL_READ_FRAMEBUFFER, previous_read_framebuffer);

    std::error_code ec;
    std::filesystem::path directory = std::filesystem::path{ data_dir() } / "screenshots";
    std::filesystem::create_directories(directory, ec);
    std::filesystem::path path = directory /
        ("screenshot_" + std::to_string(SDL_GetTicks64()) + "_dl" + std::to_string(mm64nx_display_list_count()) + ".png");
    stbi_flip_vertically_on_write(1);
    if (stbi_write_png(path.c_str(), screen_width, screen_height, 4, pixels.data(), screen_width * 4)) {
        printf("[Switch] Saved %s\n", path.c_str());
    }
    else {
        printf("[Switch] Failed to save %s\n", path.c_str());
    }
}

m64p_error nx_VidExt_GL_SwapBuffers(void) {
    static bool first_frame = true;
    if (first_frame) {
        first_frame = false;
        printf("[GLideN64] First frame presented\n");
    }
    if (mm64nx_screenshot_requested.exchange(false)) {
        save_screenshot();
    }
    SDL_GL_SwapWindow(window);
    return M64ERR_SUCCESS;
}

uint32_t nx_VidExt_GL_GetDefaultFramebuffer(void) { return 0; }

m64p_error CorePluginGetVersion(m64p_plugin_type* type, int* version, int* api_version, const char** name, int* caps) {
    if (type) *type = M64PLUGIN_CORE;
    if (version) *version = 0x020600;
    if (api_version) *api_version = 0x020001;
    if (name) *name = "DK64NX core";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

// GLideN64 looks up the core's functions with dlsym. Horizon has no dynamic libraries, so this is dlsym itself there; on
// the desktop the system's dlsym is still needed (GL and SDL load their own functions with it), so GLideN64's
// MupenPlusAPIImpl.cpp is compiled with dlsym renamed to this instead (switch/switch.cmake).
#ifdef __SWITCH__
#define CORE_DLSYM dlsym
#else
#define CORE_DLSYM mm64nx_core_dlsym
#endif

void* CORE_DLSYM(void*, const char* name) {
    struct Entry { const char* name; void* fn; };
    static const Entry table[] = {
#define CORE_FN(fn) {#fn, reinterpret_cast<void*>(&nx_##fn)}
        CORE_FN(ConfigOpenSection), CORE_FN(ConfigDeleteSection), CORE_FN(ConfigSaveSection),
        CORE_FN(ConfigSaveFile), CORE_FN(ConfigSetDefaultInt), CORE_FN(ConfigSetDefaultFloat),
        CORE_FN(ConfigSetDefaultBool), CORE_FN(ConfigSetDefaultString), CORE_FN(ConfigGetParamInt),
        CORE_FN(ConfigGetParamFloat), CORE_FN(ConfigGetParamBool), CORE_FN(ConfigGetParamString),
        CORE_FN(ConfigSetParameter), CORE_FN(ConfigGetParameter), CORE_FN(ConfigGetParameterHelp),
        CORE_FN(ConfigExternalOpen), CORE_FN(ConfigExternalClose), CORE_FN(ConfigExternalGetParameter),
        CORE_FN(ConfigGetSharedDataFilepath), CORE_FN(ConfigGetUserConfigPath),
        CORE_FN(ConfigGetUserDataPath), CORE_FN(ConfigGetUserCachePath),
        CORE_FN(VidExt_Init), CORE_FN(VidExt_Quit), CORE_FN(VidExt_ListFullscreenModes),
        CORE_FN(VidExt_ListFullscreenRates), CORE_FN(VidExt_SetVideoMode),
        CORE_FN(VidExt_SetVideoModeWithRate), CORE_FN(VidExt_ResizeWindow), CORE_FN(VidExt_SetCaption),
        CORE_FN(VidExt_ToggleFullScreen), CORE_FN(VidExt_GL_GetProcAddress),
        CORE_FN(VidExt_GL_SetAttribute), CORE_FN(VidExt_GL_GetAttribute),
        CORE_FN(VidExt_GL_SwapBuffers), CORE_FN(VidExt_GL_GetDefaultFramebuffer),
#undef CORE_FN
        {"PluginGetVersion", reinterpret_cast<void*>(&CorePluginGetVersion)},
    };
    for (const Entry& entry : table) {
        if (strcmp(entry.name, name) == 0) {
            return entry.fn;
        }
    }
    fprintf(stderr, "[DK64NX] core symbol not provided: %s\n", name);
    return nullptr;
}

} // extern "C"

// Values GLideN64 must not pick by itself on a 720p Tegra X1 (ConfigSetDefault* keeps them).
void mm64nx_seed_gliden64_config() {
    m64p_handle general;
    m64p_handle gliden;
    nx_ConfigOpenSection("Video-General", &general);
    nx_ConfigOpenSection("Video-GLideN64", &gliden);
    set_param(general, "Fullscreen", bool_param(true), false);
    set_param(general, "ScreenWidth", int_param(screen_width), false);
    set_param(general, "ScreenHeight", int_param(screen_height), false);
    set_param(general, "VerticalSync", bool_param(true), false);
    set_param(gliden, "UseNativeResolutionFactor", int_param(resolution_factor), false);
    // 3 = adjust: 3D widened to 16:9, 2D kept at its aspect and placed by the RT64 alignment commands in the game's
    // patches (src/switch/gliden64/gliden64_rt64ex.cpp), like RT64's "Expand" aspect ratio on PC.
    set_param(gliden, "AspectRatio", int_param(3), false);
    // GL calls on their own thread: the driver's work for each draw call (most of the passes' time, measured with
    // mm64nx_profile.h) runs on another core. DK64Recomp-NX measured it as slower for DK64; testing it for Mega Man 64.
    set_param(gliden, "ThreadedVideo", bool_param(true), false);
    set_param(gliden, "txHiresEnable", bool_param(false), false);
    set_param(gliden, "ShowFPS", bool_param(false), false);
    // The game builds its intro's sky and water from texture rectangle tiles, which showed seams at 3x: force
    // continuous texrect coordinates and keep their sampling inside each tile.
    set_param(gliden, "CorrectTexrectCoords", int_param(2), false);
    set_param(gliden, "EnableTexCoordBounds", int_param(1), false);
    // Copying every frame back to RDRAM stalls each pass by about 12 ms (measured by DK64Recomp-NX), which doesn't fit
    // two passes per frame at 60 fps.
    set_param(gliden, "EnableCopyColorToRDRAM", int_param(0), false);
    // Copying depth to RDRAM renders depth on the CPU every frame, which the game doesn't need.
    set_param(gliden, "EnableCopyDepthToRDRAM", int_param(0), false);

    // For testing: MM64NX_GLIDEN64_SET="Name=value,Name=value" overrides integer settings of the Video-GLideN64 section.
    if (const char* overrides = getenv("MM64NX_GLIDEN64_SET")) {
        std::string list{ overrides };
        size_t pos = 0;
        while (pos < list.size()) {
            size_t end = list.find(',', pos);
            std::string entry = list.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            size_t equals = entry.find('=');
            if (equals != std::string::npos) {
                std::string name = entry.substr(0, equals);
                int value = atoi(entry.c_str() + equals + 1);
                set_param(gliden, name.c_str(), int_param(value), false);
                printf("[GLideN64] %s = %d\n", name.c_str(), value);
            }
            if (end == std::string::npos) {
                break;
            }
            pos = end + 1;
        }
    }
}
