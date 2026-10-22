# Building and technical notes

How to build Mega Man 64 Recomp NX, and how the Switch layer works. For installing and playing, see the
[main README](../README.md).

## Building

Install:

- devkitPro with `switch-dev`, `switch-sdl2` and `switch-mesa` (OpenGL through mesa, EGL, libdrm_nouveau).
- CMake, Ninja, Python 3 and a host C/C++ compiler.
- clang with the MIPS backend and `ld.lld`, for the game patches.

Put your own unmodified USA ROM at the repository root as `MegaMan64.us.rev1.z64`
(SHA-1 `f24fe0aff01aec018e2dd558ec4f076cf328129f`), then run:

```sh
switch/build.sh
```

The script:

1. applies the Switch patches to the submodules (`switch/patches/`: N64ModernRuntime, N64Recomp and GLideN64), so
   the submodules themselves stay on their upstream commits;
2. builds the host tools (N64Recomp, RSPRecomp, file_to_c);
3. generates the game code from your ROM (`RecompiledFuncs/`, `RecompiledPatches/`, `rsp/aspMain.cpp`);
4. builds `build-switch/MegaMan64Recompiled.nro` and copies it to `build-switch/dist/`.

Code generated from the ROM is ignored by git and must never be committed or distributed.

`-DMM64NX_PROFILING=ON` adds timers to GLideN64's main stages (`src/switch/gliden64/mm64nx_profile.h`), logged every
10 seconds.

### Testing on a console

Start the Homebrew Menu in title mode, press **Y** for netloader, then:

```sh
nxlink -s -p MegaMan64Recompiled/MegaMan64Recompiled.nro build-switch/MegaMan64Recompiled.nro
```

`-p` replaces the installed copy instead of saving a second one in `switch/`; `-s` streams the game's output (crash
reports included) back to the terminal.

## Layout

- `src/switch/`: the Switch layer.
  - `nx_platform.cpp`: libnx glue (thread stacks, nxlink, crash handler, applet mode check).
  - `nx_controls.cpp`: the controller layout, the 30/60 fps setting and the right stick.
  - `nx_jit.c`: executable memory for the live recompiler.
  - `ui_stubs.cpp`: stand-ins for the RmlUi menus the PC version has.
  - `gliden64/`: GLideN64 as the renderer: the render context, a minimal mupen64plus core, and the RT64 extended GBI
    decoder with frame interpolation (from DK64Recomp-NX).
- `switch/switch.cmake`: the build, included by the top-level `CMakeLists.txt` when using devkitPro's Switch toolchain.
- `switch/patches/`: changes to the submodules.
- `src/game/input.cpp`: the Switch controller layout (`switch_psx_controller_mappings`).

## How it works

The PC version draws with RT64, which needs Vulkan, D3D12 or Metal; Switch homebrew has none of them. GLideN64 on
OpenGL (mesa nouveau) takes its place behind N64ModernRuntime's `RendererContext`, reading the same mupen64plus
`GFX_INFO` view of the recompiled game's memory. The game's patches emit RT64's extended GBI (opcode `0x64`) for
widescreen and interpolation, which `gliden64_rt64ex.cpp` decodes.

### Problems found on hardware

| Symptom | Cause | Fix |
|---|---|---|
| Abort at startup | libnx can't detach threads | `__syscall_thread_detach` provided as a no-op |
| Crashes in game threads | `std::thread` stacks are 128 KiB on libnx | `__syscall_thread_create` wrapped for 1 MiB stacks |
| Null pointer in the VI thread at boot | Game started before the VI thread set up a video mode | Game started from the VI callback |
| High-pitched audio | Upstream's latency guard measured the queue at the game's sample rate | Measured at the output rate |
| Music cutting out | 256-frame SDL buffers | 1024-frame buffers on Switch |
| Saves failing | libstdc++'s `copy_file` ends with `fchmod`, which the SD card driver rejects | Files rotated with renames |
| Patched functions not taking effect | GCC's `RECOMP_FUNC` wasn't weak | Made weak, as on clang |
| Capcom logo cut in half | Patch scissors use 320×240 coordinates, the logo is drawn at 640×480 | Extended scissor height scaled with the image width |
| Seams in the sky and water | Texture rectangle tiles at 3× | `CorrectTexrectCoords` and `EnableTexCoordBounds` |
| Empty sides behind menus in 16:9 | 2D backgrounds are drawn for 320 pixels | Background rows continued into the widescreen area (`gDP.cpp`), the main menu's floor widened in `patches/ui_patches.c` |
| Freeze with threaded video | Shaders were saved from inside a buffer swap, on GLideN64's GL thread | Saved from the graphics thread |
| Slow at 60 fps | OpenGL driver work on the graphics thread | GLideN64's threaded video |
| Thin lines between ground tiles (grass, mountains) | Each terrain tile has its own matrix with an integer translation, so neighbours can land up to a unit apart | Tiles scaled up slightly while their matrix is converted (`patches/terrain_seams.c`, upstream PR #20) |
| Freeze when testing over nxlink while idle | The log socket blocked when the console's Wi-Fi stalled, and the graphics thread waited on it | The nxlink socket made non-blocking |
| Textures turning black during a session | nvdrv's 8 MiB transfer memory fills up past about 5,000 textures | `__nx_nv_transfermem_size` raised to 32 MiB (from DK64Recomp-NX) |

### Platform notes

- **Memory:** the runtime normally reserves 4 GB of address space and commits 512 MB for RDRAM. Horizon has no `mmap`
  and commits memory eagerly, so the Switch build allocates 64 MB directly (16 MB of RDRAM and patch area, plus a
  48 MB heap for the patches), without the guard region. The game still needs title mode for the renderer's memory.
- **Threads:** ultramodern lets its threads run on cores 0-2.
- **Code mods:** native libraries can't be loaded and the executable's code can't be patched, so code mods are
  unsupported.

## Desktop build

`-DMM64NX_DESKTOP=ON` builds the same GLideN64 layer for desktop Linux. It's a development tool, not a release
target: it renders exactly like the Switch, so graphics bugs can be reproduced and checked without a console. It runs
offscreen (`SDL_VIDEODRIVER=offscreen`) and takes environment variables for timed screenshots
(`MM64NX_SHOT_EVERY_MS`), scripted input (`MM64NX_INPUT_SCRIPT`), memory snapshots (`MM64NX_RAM_DUMP_MS`), display
list dumps (`MM64NX_DL_DUMP`) and GLideN64 settings (`MM64NX_GLIDEN64_SET`); see `src/switch/desktop_platform.cpp`.
