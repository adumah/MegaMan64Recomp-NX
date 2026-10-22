# Mega Man 64 Recomp NX

**A Port from a Static recompilation from N64: [Mega Man 64: Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled)
running natively on the Nintendo Switch as homebrew (`.nro`).**

The game's MIPS code is translated to C ahead of time by [N64Recomp](https://github.com/N64Recomp/N64Recomp) and runs
natively on the Switch's CPU, on top of [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), which
reimplements the N64 system the game expects. There is no CPU emulation. Graphics are drawn by
[GLideN64](https://github.com/gonetz/GLideN64) on OpenGL, as in [DK64Recomp-NX](https://github.com/Trinalord777/DK64Recomp-NX).

> [!IMPORTANT]
> **This repository contains no disc, ROM or game files.** No ROM, disc image, graphics, audio or other data from the
> game is included, and none of it may be added. The release `.nro` contains the port and the recompiled program code,
> but no game data either. You need your own legally obtained copy of **Mega Man 64 (USA)**: the game reads all its
> data from your ROM.

## Features

| | |
|---|---|
| **Native recompilation** | The original game code, recompiled for the Switch's ARM CPU. |
| **Widescreen** | 16:9, with the HUD and 2D menus kept in their original proportions. Full-screen backgrounds (Capcom logo, main menu, pause menu) are extended into the widescreen area. |
| **Upscaled resolution** | Rendered at 3× the N64's resolution (960×720). Textures are the game's originals. |
| **No seams in the terrain** | The thin lines between ground tiles that appear at high resolutions are hidden (fix from [upstream PR #20](https://github.com/MegaMan64Recomp/MegaMan64Recompiled/pull/20)). |
| **30 fps, 60 fps optional** | The game's original 30 fps by default. **L3 + R3** switches to 60 fps (see the warning below). The choice is remembered. |
| **No overclock** | The port runs at the console's standard clocks and never changes them. |
| **PlayStation-style controls** | A layout based on Mega Man Legends on the PlayStation (see [Controls](#controls)). |
| **Right stick** | Turns the view, turns Mega Man around and locks on (see [Right stick](#right-stick)). |

### ⚠️ About 60 fps

At 60 fps every game frame is drawn twice: the real frame and an interpolated in-between one. When a frame is too
heavy to draw twice in time, it's shown without its in-between image, so the game itself never slows down; the picture
just gets closer to 30 fps in that moment.

**60 fps is barely reachable on this renderer.** At the Switch's standard clocks only menus and lighter areas reach a
full 60 fps. Even with a CPU overclock, gameplay gets its in-between image only around **70%** of the time. This port
never overclocks; if you overclock with your own tools, **you do it at your own risk**: it raises heat, power draw and
battery drain, and the author is not responsible for any damage to your console or data. The default 30 fps is how the
game was designed to run.

### Planned: a deko3d renderer for full 60 fps without overclocking

The current renderer (GLideN64 on OpenGL) is limited by the Switch's CPU. A native **deko3d** renderer is planned: the
PC version's renderer, RT64, running on the Switch's own graphics API, which moves the work from the CPU to the GPU.
The goal is a full 60 fps at the standard clocks, with no overclock, as [RaceWave46-Switch](https://github.com/DomazinUS/RaceWave46-Switch)
does for Wave Race 64. It's a large project and there is no date for it; GLideN64 stays until it's ready.

## What you need

- A Switch running custom firmware (Atmosphère) with the Homebrew Menu.
- Your own **Mega Man 64 (USA)** ROM in `.z64` format, SHA-1 `f24fe0aff01aec018e2dd558ec4f076cf328129f`.

## Installing

1. Download `MegaMan64Recompiled.nro` from the [releases](../../releases).
2. Copy it to `sdmc:/switch/MegaMan64Recompiled/` on the SD card.
3. Put your ROM in the same folder, named `MegaMan64.us.rev1.z64`.
4. Start the Homebrew Menu in **title mode**: hold **R** while launching any installed game, then pick
   **Mega Man 64 Recomp NX**. Launched from the Album (applet mode) the game doesn't get enough memory and refuses to
   start.

The game starts directly; there's no launcher menu yet. Configuration and saves are stored in
`sdmc:/switch/MegaMan64Recompiled/`.

## Controls

> [!NOTE]
> **The controls don't match the game's own instructions.** The game's tutorials, prompts and manual describe the N64
> controller's buttons. This port uses a PlayStation-style layout instead, so the buttons named in the game are not
> the ones to press on the Switch.

| Switch | PlayStation | Action |
|---|---|---|
| B (bottom) | × | Jump |
| Y (left) | □ | Buster |
| X (top) | △ | Special weapon |
| A (right) | ○ | Talk / interact |
| Left stick up / down | Left stick | Move forward / back |
| Left stick left / right | L1 / R1 | Sidestep left / right |
| L / R | | Run left / right |
| D-pad | D-pad | Move |
| ZR | R2 | Lock on |
| ZL or R3 | L2 | Look |
| − (Minus) | Select | Map |
| + (Plus) | Start | Pause |
| L3 + R3 | | Switch between 30 and 60 fps (one rumble: 30, two: 60) |

### Right stick

| Right stick | Action |
|---|---|
| Left / right | Turns the view: Mega Man turns and the camera follows behind him. The further you push, the faster. |
| Down | Turns Mega Man around (180°). |
| Up (hold) | Locks on, like ZR. |

Turning only works while you control Mega Man (not in menus, cutscenes or dialogue).

## Known issues

- There's no settings menu yet: the game starts immediately, and the frame rate is switched with L3 + R3.
- The first time a new effect appears, there can be a short stutter while its shader is compiled. Shaders are saved
  to the SD card, so it doesn't happen again in later sessions.
- Mods are not supported.
- Not every area of the game has been tested yet. Please report graphical problems with where they happen.

## Building

See [switch/README.md](switch/README.md). In short: install devkitPro (`switch-dev`, `switch-sdl2`, `switch-mesa`),
CMake, Ninja, a host compiler, and clang with the MIPS backend plus `ld.lld`; put your ROM at the repository root as
`MegaMan64.us.rev1.z64`; then run `switch/build.sh`. Code generated from the ROM is ignored by git and must never be
committed.

## Credits

**Mega Man 64** (Mega Man Legends) was developed and published by **Capcom**, originally on the PlayStation and
brought to the Nintendo 64 in 2001. Nintendo 64 and Nintendo Switch are platforms by **Nintendo**. All rights to the
game belong to Capcom.

This port only adds the Switch layer. The hard work is the work of these projects:

- **[Mega Man 64: Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled)** and its contributors: the
  recompiled game, its patches and enhancements, built on the
  **[Mega Man 64 decompilation](https://github.com/MegaMan64Recomp/MegaMan64Decomp)**.
- **[N64Recomp](https://github.com/N64Recomp/N64Recomp)** and
  **[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)** (Wiseguy and contributors): the static
  recompiler and the N64 runtime.
- **[RT64](https://github.com/rt64/rt64)** (Darío and contributors): the PC version's renderer and the extended GBI
  that the game's widescreen and interpolation patches use.
- **[GLideN64](https://github.com/gonetz/GLideN64)** (gonetz and contributors): the renderer of this port.
- **[DK64Recomp-NX](https://github.com/Trinalord777/DK64Recomp-NX)** (Trinalord777): the GLideN64 integration, its
  RT64 extended GBI decoder and frame interpolation, and the fixes for running N64ModernRuntime on Horizon, which this
  port builds on.
- **[RaceWave46-Switch](https://github.com/DomazinUS/RaceWave46-Switch)** (DomazinUS): reference for N64
  recompilations on the Switch, and the proof that a deko3d renderer can reach 60 fps without overclocking.
- **[randygil](https://github.com/randygil)**: the terrain seam fix
  ([PR #20](https://github.com/MegaMan64Recomp/MegaMan64Recompiled/pull/20)) and
  [MegaMan64RecompiledVR](https://github.com/randygil/MegaMan64RecompiledVR).
- **[MegaMan64Recompiled-Mobile](https://github.com/DevZer0D4Y/MegaMan64Recompiled-Mobile)** (DevZer0D4Y): the Android
  port, used as a reference.
- **[Mega Man Legends Recompiled](https://github.com/VuloB1/MegaManLegendsRecomp)** (VuloB1): the idea for the right
  stick controls.
- **[devkitPro](https://devkitpro.org)**, **[libnx](https://github.com/switchbrew/libnx)**, SDL2 and Mesa: the Switch
  toolchain and libraries.

## How it was made

This port was developed with the help of an AI coding agent, Claude (Anthropic), through Claude Code, under the
direction of its author, [adumah](https://github.com/adumah), who chose what to build and tested every build on a real
Switch.

## License

GPL-3.0, like Mega Man 64: Recompiled. GLideN64 is licensed under the GPL version 2 only, which isn't compatible
with the GPL-3.0 inside one binary; DK64Recomp-NX notes the same about its builds.

## Disclaimer

This is an unofficial fan project, not affiliated with or endorsed by Capcom or Nintendo. Mega Man is a trademark of
Capcom; Nintendo Switch and Nintendo 64 are trademarks of Nintendo. All trademarks belong to their respective owners.
No game assets are included or distributed. Use this software at your own risk.
