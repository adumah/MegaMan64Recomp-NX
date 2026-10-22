#ifndef __NX_JIT_H__
#define __NX_JIT_H__

// Force-included into the live recompiler's sources (see switch/switch.cmake) to replace sljit's
// mmap-based executable allocator, which doesn't exist on Horizon.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* mm64nx_jit_malloc_exec(size_t size);
void mm64nx_jit_free_exec(void* ptr);
ptrdiff_t mm64nx_jit_exec_offset(void* ptr);

#ifdef __cplusplus
}
#endif

#define SLJIT_EXECUTABLE_ALLOCATOR 0
#define SLJIT_UTIL_STACK 0
#define SLJIT_MALLOC_EXEC(size, exec_allocator_data) mm64nx_jit_malloc_exec(size)
#define SLJIT_FREE_EXEC(ptr, exec_allocator_data) mm64nx_jit_free_exec(ptr)
#define SLJIT_EXEC_OFFSET(ptr) mm64nx_jit_exec_offset(ptr)

#endif
