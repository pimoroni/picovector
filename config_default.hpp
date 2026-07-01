#pragma once

// Default PicoVector configuration — the standalone backstop.
//
// Every knob is #ifndef-guarded, so an embedder can override any of them by
// defining it first (e.g. via a picovector.config.hpp on the include path, or
// -D on the compiler command line). The core headers always include this file
// last, after any embedder config, so it only fills in what wasn't set.

// ── allocators / containers ─────────────────────────────────────────────────
#ifndef PV_STD_ALLOCATOR
#define PV_STD_ALLOCATOR std::allocator
#endif
#ifndef PV_MALLOC
#define PV_MALLOC malloc
#endif
#ifndef PV_MALLOC_NO_SCAN
#define PV_MALLOC_NO_SCAN malloc
#endif
#ifndef PV_FREE
#define PV_FREE free
#endif
#ifndef PV_REALLOC
#define PV_REALLOC realloc
#endif

// ── build knobs (both off by default; embedders opt in) ─────────────────────
// Rasteriser phase profiling (prints phase timings).
#ifndef PV_PROFILE
#define PV_PROFILE 0
#endif
// Use the second core (core1) for rasterisation. Off by default; projects that
// can spare core1 (e.g. Badgeware) define PV_DUAL_CORE=1 to roughly halve render
// time.
#ifndef PV_DUAL_CORE
#define PV_DUAL_CORE 0
#endif

// Minimum blit size (source/destination pixel count) before image_t::blit splits
// its rows across both cores. Below this the fixed inter-core handshake cost
// outweighs the win; large scaled/filtered blits benefit most (compute-bound),
// plain opaque copies least (shared-bus bandwidth-bound). Only used when
// PV_DUAL_CORE=1.
#ifndef PV_DUAL_CORE_BLIT_MIN_PX
#define PV_DUAL_CORE_BLIT_MIN_PX (64 * 64)
#endif

// ── working buffer size ─────────────────────────────────────────────────────
// Scratch pool for rasterisation (tile/node/edge buffers). The core rasteriser
// needs ~52 KB (4 KB tile + 32 KB nodes + edge accumulator); 60 KB gives
// headroom. Embedders that also use the buffer as scratch — e.g. PNG/JPEG decode
// in the MicroPython bindings — should enlarge this (see PicoVector_working_buffer).
#ifndef PV_WORKING_BUFFER_SIZE
#define PV_WORKING_BUFFER_SIZE (60 * 1024)
#endif
