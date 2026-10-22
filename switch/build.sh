#!/usr/bin/env bash
# Builds Mega Man 64: Recompiled for Nintendo Switch.
#
# Requires devkitPro (devkitA64, libnx, switch-sdl2), a host C/C++ toolchain, CMake, Ninja, and clang with the MIPS
# backend plus ld.lld for the game patches. Place your ROM at the repository root as MegaMan64.us.rev1.z64.
#
# Environment overrides:
#   PATCHES_CC  C compiler for the MIPS patches (default: clang)
#   PATCHES_LD  linker for the MIPS patches (default: ld.lld)
#   BUILD_TYPE  CMake build type for the Switch build (default: Release)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PATCHES_CC="${PATCHES_CC:-clang}"
PATCHES_LD="${PATCHES_LD:-ld.lld}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
ROM="MegaMan64.us.rev1.z64"
ROM_SHA1="f24fe0aff01aec018e2dd558ec4f076cf328129f"

apply_patch() {
    local repo="$1" patch="$2"
    if git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
        return
    fi
    echo "Applying $(basename "$patch") to $repo"
    git -C "$repo" apply "$patch"
}

echo "== Applying Switch patches to dependencies"
apply_patch lib/N64ModernRuntime "$ROOT/switch/patches/runtime-switch.patch"
apply_patch lib/N64ModernRuntime/N64Recomp "$ROOT/switch/patches/n64recomp-switch.patch"
apply_patch lib/GLideN64 "$ROOT/switch/patches/gliden64.patch"

echo "== Building host tools"
cmake -S lib/N64ModernRuntime/N64Recomp -B build-host/recompiler -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
cmake --build build-host/recompiler --target N64RecompCLI RSPRecomp
cmake -S lib/rt64/src/tools/file_to_c -B build-host/file_to_c -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-host/file_to_c

echo "== Generating game code"
if [ ! -f "$ROM" ]; then
    echo "Missing $ROM at the repository root." >&2
    exit 1
fi
if [ "$(sha1sum "$ROM" | cut -d' ' -f1)" != "$ROM_SHA1" ]; then
    echo "$ROM doesn't match the expected SHA-1 ($ROM_SHA1). Use an unmodified USA ROM in .z64 format." >&2
    exit 1
fi
build-host/recompiler/N64Recomp us.rev1.toml
build-host/recompiler/RSPRecomp aspMain.us.rev1.toml
# Newer clang versions treat incompatible pointer types as an error, which the patches haven't been updated for.
make -C patches CC="$PATCHES_CC -Wno-error=incompatible-pointer-types" LD="$PATCHES_LD"
build-host/recompiler/N64Recomp patches.toml
build-host/file_to_c/file_to_c patches/patches.bin mm_patches_bin RecompiledPatches/patches_bin.c RecompiledPatches/patches_bin.h

echo "== Building for Switch"
cmake -S . -B build-switch -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
cmake --build build-switch

echo "== Packaging"
DIST="build-switch/dist/switch/MegaMan64Recompiled"
rm -rf build-switch/dist
mkdir -p "$DIST"
cp build-switch/MegaMan64Recompiled.nro "$DIST/"
echo "Copy the contents of build-switch/dist to the root of your SD card, then put your ROM in"
echo "SD:/switch/MegaMan64Recompiled/ named $ROM."
