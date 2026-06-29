#pragma once

// pv_metrics.hpp — optional per-binding call-count + timing instrumentation.
//
// Enabled at build time with -DPV_METRICS=1 (CMake option PV_METRICS). When
// off, this header defines nothing beyond PV_METRIC_COUNT and the generated
// indices, the per-binding `pv::metric_scope` guards are #if'd out, and the
// storage table + name table are not compiled — so it is genuinely zero cost.
//
// Each generated binding function (and any native body that opts in) carries:
//
//     #if PV_METRICS
//     pv::metric_scope _pvm(PV_M_image_circle);
//     #endif
//
// The RAII scope increments the call count in its constructor and accumulates
// elapsed microseconds in its destructor. Because mp_raise_* unwinds via a
// longjmp (nlr), the destructor is skipped on a *raised* call — so error paths
// still increment the call count but do not contribute to the timing total.

#include "pv_metrics_table.h"   // generated: PV_M_* indices, PV_METRIC_COUNT, PV_METRICS default

#if PV_METRICS

extern "C" {
  #include "py/mphal.h"   // mp_hal_ticks_us()
}

#include <stdint.h>

struct pv_metric_t {
  uint32_t calls;
  uint64_t usec;
};

// Storage (BSS, zero-initialised) and the generated parallel name table.
// Neither holds GC-managed pointers, so the GC never needs to scan them.
extern pv_metric_t pv_metrics[PV_METRIC_COUNT];
extern const char *const pv_metric_names[PV_METRIC_COUNT];

namespace pv {

  struct metric_scope {
    uint32_t idx;
    mp_uint_t t0;
    explicit metric_scope(uint32_t i) : idx(i), t0(mp_hal_ticks_us()) {
      pv_metrics[i].calls++;
    }
    ~metric_scope() {
      // 32-bit unsigned subtraction yields the correct delta across a single
      // wrap as long as the call took < ~71 minutes (it didn't).
      pv_metrics[idx].usec += (mp_uint_t)(mp_hal_ticks_us() - t0);
    }
  };

}  // namespace pv

#endif  // PV_METRICS
