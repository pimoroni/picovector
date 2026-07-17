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
// The allocator (PV_MALLOC/PV_FREE/PV_STD_ALLOCATOR) is a tracing GC that reclaims
// unreferenced blocks on its own. Off by default (bare/host builds own and free
// their allocations). Embedders on a GC — e.g. the MicroPython build wiring these
// onto m_malloc/m_free — define PV_GC_MANAGED=1 so picovector never explicitly frees
// or runs a finaliser that frees: doing so double-frees during the GC sweep.
#ifndef PV_GC_MANAGED
#define PV_GC_MANAGED 0
#endif

// On the device, do the per-channel IIR lerp with an RP2350 hardware interpolator
// (blend mode). We also benchmarked a hand-tuned M33 SIMD version; it was no faster
// (~21ms/frame either way — the blur is limited by per-pixel bus/compute throughput,
// not the arithmetic), so the interpolator wins on readability. Host builds have no
// interpolator, so PV_BLUR_INTERP=0 falls back to portable C++.
#ifndef PV_BLUR_INTERP
#define PV_BLUR_INTERP PV_DUAL_CORE
#endif

// core1 is available (but may not be owned by picovector) on the device build;
// enable PV_BLUR_DUAL_CORE to split each pass across both cores.
#ifndef PV_BLUR_DUAL_CORE
#define PV_BLUR_DUAL_CORE PV_DUAL_CORE
#endif

// Minimum blit size (source/destination pixel count) before image_t::blit splits
// its rows across both cores. Below this the fixed inter-core handshake cost
// outweighs the win; large scaled/filtered blits benefit most (compute-bound),
// plain opaque copies least (shared-bus bandwidth-bound). Only used when
// PV_DUAL_CORE=1.
#ifndef PV_DUAL_CORE_BLIT_MIN_PX
#define PV_DUAL_CORE_BLIT_MIN_PX (64 * 64)
#endif

// ── clock source ────────────────────────────────────────────────────────────
// Current time, used by the tween module's self-timing helpers (start/now/done).
// Expands to an expression yielding the current time in whatever unit tween
// durations are expressed in. Embedders point it at their tick counter (on the
// badge, milliseconds from the input module — the same source as badge.ticks);
// the standalone default is a frozen 0 so timing is inert until wired.
#ifndef PV_TICKS
#define PV_TICKS 0
#endif

// ── working buffer size ─────────────────────────────────────────────────────
// Scratch pool for rasterisation (tile/node/edge buffers). The core rasteriser
// needs ~52 KB (4 KB tile + 32 KB nodes + edge accumulator); 60 KB gives
// headroom. Embedders that also use the buffer as scratch — e.g. PNG/JPEG decode
// in the MicroPython bindings — should enlarge this (see PicoVector_working_buffer).
#ifndef PV_WORKING_BUFFER_SIZE
#define PV_WORKING_BUFFER_SIZE (60 * 1024)
#endif
