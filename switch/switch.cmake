# GLideN64 build: Nintendo Switch (libnx), when using devkitPro's Switch toolchain, or the same renderer on a desktop
# Linux PC (-DMM64NX_DESKTOP=ON) to debug graphics and reverse engineer the game without a console. Included from the
# top-level CMakeLists.txt.
#
# Game code (RecompiledFuncs, RecompiledPatches, rsp/aspMain.cpp) must be generated on the host beforehand,
# see switch/README.md.

set(MM64NX_SOURCE_DIR ${CMAKE_SOURCE_DIR})

# Generated sources that have to exist before configuring.
foreach(GENERATED_FILE
        RecompiledFuncs/recomp_overlays.inl
        RecompiledPatches/patches.c
        RecompiledPatches/patches_bin.c
        RecompiledPatches/recomp_overlays.inl
        rsp/aspMain.cpp)
    if (NOT EXISTS ${MM64NX_SOURCE_DIR}/${GENERATED_FILE})
        message(FATAL_ERROR "Missing generated source ${GENERATED_FILE}. Generate the game code on the host first (see switch/README.md).")
    endif()
endforeach()

option(MM64NX_PROFILING "Time GLideN64's stages and log them every 10 s (src/switch/gliden64/mm64nx_profile.h)." OFF)
if (MM64NX_PROFILING)
    add_compile_definitions(MM64NX_PROFILING)
endif()

# newlib hides POSIX extensions (fileno, fdopen...) when compiling in strict standard mode, which the runtime uses.
# MM64NX_GLIDEN64 marks code shared by the Switch and desktop GLideN64 builds; __SWITCH__ is for libnx only.
add_compile_definitions(_GNU_SOURCE MM64NX_GLIDEN64)

add_subdirectory(${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime)

if (NINTENDO_SWITCH)
    # The live recompiler (used for code mods) can't use sljit's mmap-based allocator on Horizon,
    # so it's configured to use the application's JIT allocator from src/switch/nx_jit.c instead.
    target_compile_options(LiveRecomp PRIVATE -include ${MM64NX_SOURCE_DIR}/src/switch/nx_jit.h)
endif()

# RecompiledFuncs - Library containing the primary recompiler output
add_library(RecompiledFuncs STATIC)

target_compile_options(RecompiledFuncs PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)

target_include_directories(RecompiledFuncs PRIVATE
    ${MM64NX_SOURCE_DIR}/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${MM64NX_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${MM64NX_SOURCE_DIR}/RecompiledFuncs/*.cpp)
target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib - Library containing the recompiled output for any custom function patches
add_library(PatchesLib STATIC)

target_compile_options(PatchesLib PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)

target_include_directories(PatchesLib PRIVATE
    ${MM64NX_SOURCE_DIR}/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

target_sources(PatchesLib PRIVATE
    ${MM64NX_SOURCE_DIR}/RecompiledPatches/patches.c
    ${MM64NX_SOURCE_DIR}/RecompiledPatches/patches_bin.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# GLideN64 renders on OpenGL through EGL, as a mupen64plus video plugin driven by src/switch/gliden64/.
# Built as in DK64Recomp-NX: no texture packs (NOHQ) or on-screen font (NO_OSD), and on the Switch the ARM64 code paths
# (hardware CRC32, NEON) Android builds use.
set(GLIDEN64_DIR ${MM64NX_SOURCE_DIR}/lib/GLideN64/src)
include(${MM64NX_SOURCE_DIR}/switch/gliden64_sources.cmake)
if (NINTENDO_SWITCH)
    list(REMOVE_ITEM GLIDEN64_SOURCES ${GLIDEN64_DIR}/CRC32.cpp ${GLIDEN64_DIR}/3DMath.cpp ${GLIDEN64_DIR}/RSP_LoadMatrix.cpp)
    list(APPEND GLIDEN64_SOURCES
        ${GLIDEN64_DIR}/CRC32_ARMV8.cpp
        ${GLIDEN64_DIR}/Neon/3DMathNeon.cpp
        ${GLIDEN64_DIR}/Neon/gSPNeon.cpp
        ${GLIDEN64_DIR}/Neon/RSP_LoadMatrixNeon.cpp
    )
endif()
add_library(GLideN64 STATIC ${GLIDEN64_SOURCES} ${MM64NX_SOURCE_DIR}/src/switch/gliden64/gliden64_rt64ex.cpp)
target_include_directories(GLideN64 PRIVATE
    ${GLIDEN64_DIR}
    ${GLIDEN64_DIR}/inc
    ${GLIDEN64_DIR}/osal
    ${MM64NX_SOURCE_DIR}/src/switch/gliden64
)
target_compile_definitions(GLideN64 PRIVATE MUPENPLUSAPI TXFILTER_LIB OS_LINUX EGL NDEBUG)
if (NOT NINTENDO_SWITCH)
    set_source_files_properties(${GLIDEN64_DIR}/mupenplus/MupenPlusAPIImpl.cpp PROPERTIES COMPILE_DEFINITIONS "dlsym=mm64nx_core_dlsym")
endif()
if (NINTENDO_SWITCH)
    target_include_directories(GLideN64 PRIVATE
        ${MM64NX_SOURCE_DIR}/src/switch/gliden64/shim
        ${DEVKITPRO}/portlibs/switch/include
    )
    target_compile_definitions(GLideN64 PRIVATE __NEON_OPT __VEC4_OPT)
endif()
# GLideN64 targets C++17; its winlnxdefs.h #defines true/false, which breaks C++20 headers.
set_target_properties(GLideN64 PROPERTIES CXX_STANDARD 17)
target_compile_options(GLideN64 PRIVATE -Wno-narrowing $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)

# Main executable
add_executable(MegaMan64Recompiled)

target_sources(MegaMan64Recompiled PRIVATE
    ${MM64NX_SOURCE_DIR}/src/main/main.cpp
    ${MM64NX_SOURCE_DIR}/src/main/register_overlays.cpp
    ${MM64NX_SOURCE_DIR}/src/main/register_patches.cpp

    ${MM64NX_SOURCE_DIR}/src/game/input.cpp
    ${MM64NX_SOURCE_DIR}/src/game/controls.cpp
    ${MM64NX_SOURCE_DIR}/src/game/config.cpp
    ${MM64NX_SOURCE_DIR}/src/game/scene_table.cpp
    ${MM64NX_SOURCE_DIR}/src/game/debug.cpp
    ${MM64NX_SOURCE_DIR}/src/game/quicksaving.cpp
    ${MM64NX_SOURCE_DIR}/src/game/recomp_api.cpp
    ${MM64NX_SOURCE_DIR}/src/game/recomp_data_api.cpp

    ${MM64NX_SOURCE_DIR}/rsp/aspMain.cpp

    ${MM64NX_SOURCE_DIR}/src/switch/nx_controls.cpp
    ${MM64NX_SOURCE_DIR}/src/switch/gliden64/render_gliden64.cpp
    ${MM64NX_SOURCE_DIR}/src/switch/gliden64/core_nx.cpp
    ${MM64NX_SOURCE_DIR}/src/switch/ui_stubs.cpp
)

target_include_directories(MegaMan64Recompiled PRIVATE
    ${MM64NX_SOURCE_DIR}/include
    ${MM64NX_SOURCE_DIR}/src/switch
    ${MM64NX_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${MM64NX_SOURCE_DIR}/lib/concurrentqueue
    ${MM64NX_SOURCE_DIR}/lib/GamepadMotionHelpers
    ${MM64NX_SOURCE_DIR}/lib/RmlUi/Include
    ${MM64NX_SOURCE_DIR}/lib/rt64/src/contrib
    ${MM64NX_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${MM64NX_SOURCE_DIR}/lib/rt64/src
    ${MM64NX_SOURCE_DIR}/lib/SlotMap
    ${MM64NX_SOURCE_DIR}/lib/GLideN64/src/inc
    ${MM64NX_SOURCE_DIR}/lib/rt64/src/contrib
)

target_compile_definitions(MegaMan64Recompiled PRIVATE
    HLSL_CPU
    RMLUI_STATIC_LIB
    # No launcher UI yet: start the game from the ROM on the SD card.
    MM64NX_NO_LAUNCHER
)

target_compile_options(MegaMan64Recompiled PRIVATE
    -fno-strict-aliasing
    -fms-extensions
)

target_link_libraries(MegaMan64Recompiled PRIVATE
    PatchesLib
    RecompiledFuncs
    librecomp
    ultramodern
    GLideN64
    PkgConfig::SDL2
)

if (NOT NINTENDO_SWITCH)
    # Same as upstream: the RSP microcode uses SSSE3 on x86.
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        target_compile_options(MegaMan64Recompiled PRIVATE -march=nehalem)
    endif()
    target_sources(MegaMan64Recompiled PRIVATE ${MM64NX_SOURCE_DIR}/src/switch/desktop_platform.cpp)
    target_link_libraries(MegaMan64Recompiled PRIVATE EGL GL pthread dl)
    return()
endif()

target_sources(MegaMan64Recompiled PRIVATE
    ${MM64NX_SOURCE_DIR}/src/switch/nx_platform.cpp
    ${MM64NX_SOURCE_DIR}/src/switch/nx_jit.c
)

# Give threads created without an explicit stack size (all std::threads) a larger stack than libnx's 128 KiB default.
target_link_options(MegaMan64Recompiled PRIVATE -Wl,--wrap=__syscall_thread_create)
# Log abort() calls, which otherwise exit silently (see src/switch/nx_platform.cpp).
target_link_options(MegaMan64Recompiled PRIVATE -Wl,--wrap=abort)

target_link_libraries(MegaMan64Recompiled PRIVATE
    EGL
    GLESv2
    glapi
    drm_nouveau
)

nx_generate_nacp(MegaMan64Recompiled.nacp
    NAME "Mega Man 64 Recomp NX"
    AUTHOR "adumah"
    VERSION "0.9.1-nx"
)

set(MM64NX_ROMFS_DIR ${CMAKE_CURRENT_BINARY_DIR}/romfs)
file(MAKE_DIRECTORY ${MM64NX_ROMFS_DIR})
configure_file(${MM64NX_SOURCE_DIR}/recompcontrollerdb.txt ${MM64NX_ROMFS_DIR}/recompcontrollerdb.txt COPYONLY)

nx_create_nro(MegaMan64Recompiled
    NACP MegaMan64Recompiled.nacp
    ICON ${MM64NX_SOURCE_DIR}/switch/icon.jpg
    ROMFS ${MM64NX_ROMFS_DIR}
)
