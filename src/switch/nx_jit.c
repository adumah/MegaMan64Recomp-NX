#include <switch.h>

#include "nx_jit.h"

// Executable memory for the live recompiler. Horizon maps code memory twice: a writable view that sljit emits
// code into and an executable view that the code runs from, so allocations return pointers into the writable view
// and SLJIT_EXEC_OFFSET gives the distance to the executable one.
//
// Recompiled mod code lives until the program exits, so this is a bump allocator that never reclaims memory.

#define JIT_REGION_SIZE (32 * 1024 * 1024)
#define JIT_ALIGNMENT 64

static Jit jit;
static bool jit_initialized = false;
static bool jit_failed = false;
static size_t jit_used = 0;
static Mutex jit_mutex;

void* mm64nx_jit_malloc_exec(size_t size) {
    void* ret = NULL;

    mutexLock(&jit_mutex);

    if (!jit_initialized && !jit_failed) {
        if (R_SUCCEEDED(jitCreate(&jit, JIT_REGION_SIZE)) && R_SUCCEEDED(jitTransitionToWritable(&jit))) {
            jit_initialized = true;
        }
        else {
            jit_failed = true;
        }
    }

    size = (size + JIT_ALIGNMENT - 1) & ~(size_t)(JIT_ALIGNMENT - 1);
    if (jit_initialized && jit_used + size <= JIT_REGION_SIZE) {
        ret = (u8*)jitGetRwAddr(&jit) + jit_used;
        jit_used += size;
    }

    mutexUnlock(&jit_mutex);

    return ret;
}

void mm64nx_jit_free_exec(void* ptr) {
    (void)ptr;
}

ptrdiff_t mm64nx_jit_exec_offset(void* ptr) {
    (void)ptr;
    return (u8*)jitGetRxAddr(&jit) - (u8*)jitGetRwAddr(&jit);
}
