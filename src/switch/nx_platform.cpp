#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <typeinfo>

#include <fcntl.h>
#include <unistd.h>
#include <switch.h>

#include "nx_platform.h"

// nvdrv keeps the bookkeeping of every GPU allocation (mesa makes one per texture) in transfer memory that libnx sizes
// at 8 MiB by default. Past about 5,000 live textures every new upload fails, which showed as black textures after
// a while; GLideN64 caches up to 8,000. 32 MiB holds about 20,000 allocations (measured by DK64Recomp-NX).
extern "C" {
    u32 __nx_nv_transfermem_size = 32 * 1024 * 1024;
}

// Stack size used for threads that are created without an explicit one, which includes every std::thread.
// Recompiled game code runs on host threads and needs far more than libnx's 128 KiB default.
constexpr size_t default_thread_stack_size = 1024 * 1024;

// Crash diagnostics. Horizon homebrew exits silently on abort() (including uncaught C++ exceptions), so log
// what happened and the code addresses found on the stack. Offsets are relative to the start of the executable
// and can be symbolized with `aarch64-none-elf-addr2line -Cfe MegaMan64Recompiled.elf <offset>`.
extern "C" {
    extern char _start[];
    extern char __data_start[];

    alignas(16) uint8_t __nx_exception_stack[0x10000];
    uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

namespace {
    uintptr_t code_offset(uintptr_t addr) {
        return addr - reinterpret_cast<uintptr_t>(_start);
    }

    bool is_code_address(uintptr_t addr) {
        return addr >= reinterpret_cast<uintptr_t>(_start) && addr < reinterpret_cast<uintptr_t>(__data_start);
    }

    void dump_stack(uintptr_t sp) {
        constexpr size_t scan_words = 2048;
        constexpr size_t max_entries = 48;

        fprintf(stderr, "[Crash] Code addresses on the stack (thread %p):\n", static_cast<void*>(threadGetSelf()));

        // Don't read past the top of the current thread's stack.
        uintptr_t stack_top = sp + scan_words * sizeof(uintptr_t);
        MemoryInfo info{};
        u32 page_info;
        if (R_SUCCEEDED(svcQueryMemory(&info, &page_info, sp))) {
            stack_top = std::min<uintptr_t>(stack_top, info.addr + info.size);
        }

        size_t entries = 0;
        for (uintptr_t cur = sp; cur + sizeof(uintptr_t) <= stack_top && entries < max_entries; cur += sizeof(uintptr_t)) {
            uintptr_t value = *reinterpret_cast<const uintptr_t*>(cur);
            if (is_code_address(value)) {
                fprintf(stderr, "  0x%lx\n", code_offset(value));
                entries++;
            }
        }
    }

    [[noreturn]] void crash_exit() {
        fprintf(stderr, "[Crash] Exiting\n");
        svcExitProcess();
        __builtin_unreachable();
    }

    void terminate_handler() {
        fprintf(stderr, "[Crash] std::terminate called\n");
        if (std::exception_ptr exception = std::current_exception()) {
            try {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& e) {
                fprintf(stderr, "[Crash] Uncaught exception (%s): %s\n", typeid(e).name(), e.what());
            }
            catch (...) {
                fprintf(stderr, "[Crash] Uncaught exception of unknown type\n");
            }
        }
        dump_stack(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
        crash_exit();
    }

    const bool terminate_handler_set = (std::set_terminate(terminate_handler), true);
}

extern "C" {
    [[noreturn]] void __wrap_abort() {
        fprintf(stderr, "[Crash] abort() called from 0x%lx\n", code_offset(reinterpret_cast<uintptr_t>(__builtin_return_address(0))));
        dump_stack(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)));
        crash_exit();
    }

    void __libnx_exception_handler(ThreadExceptionDump* ctx) {
        fprintf(stderr, "[Crash] Exception 0x%x, esr 0x%x, far 0x%lx\n", ctx->error_desc, ctx->esr, ctx->far.x);
        fprintf(stderr, "[Crash] pc 0x%lx, lr 0x%lx\n",
            is_code_address(ctx->pc.x) ? code_offset(ctx->pc.x) : ctx->pc.x,
            is_code_address(ctx->lr.x) ? code_offset(ctx->lr.x) : ctx->lr.x);
        for (int i = 0; i < 29; i++) {
            fprintf(stderr, "  x%d = 0x%lx\n", i, ctx->cpu_gprs[i].x);
        }
        fprintf(stderr, "  fp = 0x%lx, sp = 0x%lx\n", ctx->fp.x, ctx->sp.x);
        dump_stack(ctx->sp.x);
        crash_exit();
    }

    int __real___syscall_thread_create(void* thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size);

    int __wrap___syscall_thread_create(void* thread, void* (*func)(void*), void* arg, void* stack_addr, size_t stack_size) {
        if (stack_addr == nullptr && stack_size == 0) {
            stack_size = default_thread_stack_size;
        }
        return __real___syscall_thread_create(thread, func, arg, stack_addr, stack_size);
    }

    // libnx doesn't implement detaching threads, so pthread_detach (and std::thread::detach) fail with ENOSYS.
    // Detached threads in the runtime live until the program exits, so treat them as detached without
    // reclaiming their resources.
    int __syscall_thread_detach(void* thread) {
        (void)thread;
        return 0;
    }

    static int nxlink_socket = -1;

    void userAppInit() {
        romfsInit();

        // Send stdout/stderr to nxlink when launched with `nxlink -s`.
        if (R_SUCCEEDED(socketInitializeDefault())) {
            nxlink_socket = nxlinkStdio();
            if (nxlink_socket < 0) {
                socketExit();
            }
            else {
                // Unbuffered so that no output is lost if the program crashes.
                setvbuf(stdout, nullptr, _IONBF, 0);
                setvbuf(stderr, nullptr, _IONBF, 0);
                // Non-blocking: when the connection stalls (the console's Wi-Fi saving power while the game sits idle),
                // log lines are dropped instead of making every thread that logs wait for the network, which froze
                // the game when the graphics thread logged.
                fcntl(nxlink_socket, F_SETFL, fcntl(nxlink_socket, F_GETFL, 0) | O_NONBLOCK);
                fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL, 0) | O_NONBLOCK);
                fcntl(STDERR_FILENO, F_SETFL, fcntl(STDERR_FILENO, F_GETFL, 0) | O_NONBLOCK);
            }
        }
    }

    void userAppExit() {
        if (nxlink_socket >= 0) {
            close(nxlink_socket);
            socketExit();
        }
        romfsExit();
    }
}

std::filesystem::path mm64nx::get_app_folder_path() {
    return std::filesystem::path{ "sdmc:/switch/MegaMan64Recompiled" };
}

bool mm64nx::is_applet_mode() {
    AppletType type = appletGetAppletType();
    return type != AppletType_Application && type != AppletType_SystemApplication;
}


void mm64nx::debug_tick() {}

uint16_t mm64nx::scripted_buttons() {
    return 0;
}

float mm64nx::scripted_right_x() {
    return 0.0f;
}
