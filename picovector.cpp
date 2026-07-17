#include "picovector.hpp"

// ---------------------------------------------------------------------------
// core1 dispatch — the component's single shared core1 worker. pv_parallel_rows
// and pv_core1_run/join let the batch blend (via _blend_spans), image_t::blit and
// the blur filter run work across both cores; the rasteriser (rasteriser.cpp)
// reaches core1 only indirectly through _blend_spans. Work is handed to core1 via
// shared memory, NOT the inter-core FIFO (MicroPython owns the FIFO's lockout IRQ);
// the FIFO is only touched — with that IRQ briefly gated — to launch the core.
// Compiled only when PV_DUAL_CORE=1.
// ---------------------------------------------------------------------------

#if PV_DUAL_CORE
extern "C" {
  // forward-declared to avoid a hard SDK include dependency (linked into the firmware)
  void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
  void irq_set_enabled(unsigned int num, bool enabled);
}
#define PV_SIO_FIFO_IRQ 25 // SIO_IRQ_FIFO on RP2350 (core0's FIFO IRQ)

namespace picovector {

  // One shared core1 worker serves two job kinds, both handed off through shared
  // memory (core0 bumps pv_go to dispatch, core1 sets pv_done when finished):
  //   KIND_PARALLEL_ROWS: core1 runs its parity half of an arbitrary per-row
  //          worker. This is how the batch blend and image_t::blit reach core1 —
  //          _blend_spans splits its span list across the two cores this way.
  //   KIND_GENERIC_FN: core1 just runs gen_fn and reports done (the blur filter's
  //          core1 band, sharing this worker instead of launching its own).
  enum pv_kind_t { KIND_PARALLEL_ROWS = 2, KIND_GENERIC_FN = 3 };
  struct pv_fill_job_t {
    int kind;
    // generic parallel-rows job (KIND_PARALLEL_ROWS): each core runs its row half
    // of an arbitrary worker (the batch blend and image_t::blit)
    pv_row_worker_t row_fn;
    void *row_ctx;
    int row_y0, row_y1;
    // generic void() job (KIND_GENERIC_FN): core1 just runs gen_fn and reports
    // done. Used by the blur filter so it shares this one core1 worker instead of
    // launching a second, conflicting one.
    void (*gen_fn)();
  };
  static pv_fill_job_t pv_job;
  static volatile uint32_t pv_go = 0;        // core0 bumps to dispatch a job
  static volatile uint32_t pv_done = 0;      // core1 sets when its work is done
  static bool pv_core1_running = false;
  static uint32_t __attribute__((aligned(8))) pv_core1_stack[1024]; // 4kB core1 stack

  static void pv_core1_entry() {
    // The M33 FPU is per-core and a bare core1 launch leaves CP10/CP11 disabled,
    // so the float math in the build would UsageFault. Enable full access first.
    *(volatile uint32_t *)0xE000ED88 |= (0xF << 20); // CPACR: CP10/CP11 = full access
    __asm volatile("dsb");
    __asm volatile("isb");

    uint32_t served = 0;
    while(true) {
      while(pv_go == served) { __asm volatile("wfe"); } // sleep until a job (no bus contention)
      served = pv_go;
      __sync_synchronize();                  // observe pv_job (written before pv_go)

      // generic parallel-rows job: no build phase, no mid barrier — just run this
      // core's odd-parity row half and report done.
      if(pv_job.kind == KIND_PARALLEL_ROWS) {
        pv_job.row_fn(pv_job.row_ctx, pv_job.row_y0 + 1, pv_job.row_y1, 2);
        __sync_synchronize();
        pv_done = served;
        __asm volatile("sev");
        continue;
      }

      // generic void() job (e.g. the blur filter's core1 band): run it and report
      // done — no build, no barrier.
      if(pv_job.kind == KIND_GENERIC_FN) {
        if(pv_job.gen_fn) pv_job.gen_fn();
        __sync_synchronize();
        pv_done = served;
        __asm volatile("sev");
        continue;
      }

      // No other job kinds are dispatched (the rasteriser reaches core1 only
      // through KIND_PARALLEL_ROWS, via _blend_spans/pv_parallel_rows), so
      // anything else just loops back to sleep.
    }
  }

  static void pv_core1_launch() {
    if(pv_core1_running) return;
    // MicroPython's lockout-victim FIFO IRQ on core0 would eat core1's launch
    // handshake, so gate it across the launch. We use shared memory at runtime,
    // so the IRQ can be restored afterwards (it simply never fires for us).
    irq_set_enabled(PV_SIO_FIFO_IRQ, false);
    multicore_launch_core1_with_stack(pv_core1_entry, pv_core1_stack, sizeof(pv_core1_stack));
    irq_set_enabled(PV_SIO_FIFO_IRQ, true);
    pv_core1_running = true;
  }

  // Split an arbitrary per-row worker across both cores by row parity. core1 runs
  // the odd-offset rows, core0 the even, then core0 blocks until core1 signals
  // done. Synchronous, and safe to call any time the rasteriser isn't mid-dispatch
  // (blits and render_flush never overlap — both fully drain core1 before
  // returning). ctx lives on the caller's stack, which stays valid because the
  // caller (core0) is parked in this function until the join.
  void pv_parallel_rows(pv_row_worker_t worker, void *ctx, int y0, int y1) {
    if(y1 - y0 < 2) { worker(ctx, y0, y1, 1); return; } // too small to split usefully

    pv_core1_launch();

    pv_job.kind = KIND_PARALLEL_ROWS;
    pv_job.row_fn = worker;
    pv_job.row_ctx = ctx;
    pv_job.row_y0 = y0;
    pv_job.row_y1 = y1;
    __sync_synchronize();                            // publish job before the go bump

    uint32_t ticket = pv_go + 1;
    pv_go = ticket; __asm volatile("sev");           // dispatch core1 (odd rows)

    worker(ctx, y0, y1, 2);                           // core0: even rows (y0, y0+2, …)

    while(pv_done != ticket) { __asm volatile("wfe"); } // join
    __sync_synchronize();                            // observe core1's writes
  }

  // Async single-job hand-off to the shared core1 worker, exposed to other
  // translation units (the blur filter). pv_core1_run() dispatches `fn` to core1
  // and returns immediately so the caller can do its own half in parallel;
  // pv_core1_join() then blocks until core1 has finished. Pair every run with a
  // join, and — like pv_parallel_rows — never overlap with a render_flush/blit
  // dispatch (they share pv_go/pv_done). extern "C" so blur.cpp can call it by
  // its plain, unmangled name.
  extern "C" void pv_core1_run(void (*fn)()) {
    pv_core1_launch();
    pv_job.kind = KIND_GENERIC_FN;
    pv_job.gen_fn = fn;
    __sync_synchronize();                            // publish job before the go bump
    pv_go = pv_go + 1; __asm volatile("sev");        // dispatch core1
  }
  extern "C" void pv_core1_join() {
    while(pv_done != pv_go) { __asm volatile("wfe"); }
    __sync_synchronize();                            // observe core1's writes
  }

}
#endif // PV_DUAL_CORE
