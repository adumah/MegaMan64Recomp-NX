// Horizon homebrew links statically, so libraries can't be loaded at run time. dlsym is real:
// GLideN64 looks up its mupen64plus "core" functions by name, and src/switch/gliden64/core_nx.cpp
// answers from a static table.
//
// From DK64Recomp-NX (switch/shim/dlfcn.h, https://github.com/Trinalord777/DK64Recomp-NX, GPL-3.0).
#ifndef MM64NX_DLFCN_H
#define MM64NX_DLFCN_H

#include <stddef.h>

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_LOCAL 0x0
#define RTLD_GLOBAL 0x100

#ifdef __cplusplus
extern "C" {
#endif

void* dlsym(void* handle, const char* name);

#ifdef __cplusplus
}
#endif

static inline void* dlopen(const char* path, int mode) { (void)path; (void)mode; return NULL; }
static inline int dlclose(void* handle) { (void)handle; return 0; }
static inline char* dlerror(void) { return (char*)"dynamic libraries are not supported on Switch"; }

#endif
