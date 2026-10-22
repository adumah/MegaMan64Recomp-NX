// Lightweight timers for GLideN64's main stages, reported by render_gliden64.cpp to measure where the Switch spends
// each frame. Each stage's time excludes the stages that run inside it, so the stages add up without double counting.
// Only built with -DMM64NX_PROFILING=ON; otherwise MM64NX_PROFILE expands to nothing.
#ifndef MM64NX_PROFILE_H
#define MM64NX_PROFILE_H

#include <chrono>
#include <cstdint>

namespace mm64nx_profile {
    enum Category {
        Vertex,     // gSPVertex: loading and transforming vertices
        Triangles,  // drawing triangles (besides the stages below)
        Rects,      // fill and texture rectangles
        States,     // render state updates (besides textures and combiners)
        Textures,   // texture cache lookups and uploads
        Combiner,   // combiner (shader) selection
        Loads,      // TMEM loads
        Count
    };

    // 0: first pass, 1: second pass (in-between image), 2: presenting. Set by render_gliden64.cpp.
    extern int pass;
    extern uint64_t exclusive_ns[3][Count];

    inline uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Scope {
        static Scope* top;  // the graphics thread is the only one drawing

        Category category;
        Scope* parent;
        uint64_t start;
        uint64_t child_ns = 0;

        explicit Scope(Category _category) : category(_category), parent(top), start(now_ns()) {
            top = this;
        }

        ~Scope() {
            const uint64_t elapsed = now_ns() - start;
            exclusive_ns[pass][category] += elapsed - child_ns;
            if (parent != nullptr) {
                parent->child_ns += elapsed;
            }
            top = parent;
        }
    };
}

#ifdef MM64NX_PROFILING
#define MM64NX_PROFILE_CONCAT2(a, b) a##b
#define MM64NX_PROFILE_CONCAT(a, b) MM64NX_PROFILE_CONCAT2(a, b)
#define MM64NX_PROFILE(category) \
    mm64nx_profile::Scope MM64NX_PROFILE_CONCAT(mm64nx_profile_scope_, __LINE__)(mm64nx_profile::category)
#else
#define MM64NX_PROFILE(category)
#endif

#endif
