// pv_metrics.cpp — the `picovector.metrics` submodule + metric storage.
//
// The per-binding instrumentation lives in the generated bindings (a
// `pv::metric_scope` guard, see pv_metrics.hpp). This file owns the storage
// table and exposes a small query/reset surface to Python:
//
//     from picovector import metrics
//     metrics.enabled            # bool — compile-time (PV_METRICS)
//     metrics.reset()            # zero all counters
//     metrics.get("image.circle")# -> (calls, total_us) or None
//     metrics.all()             # -> { name: (calls, total_us), ... }  (touched only)
//     metrics.report()          # pretty table, sorted by total time desc
//
// The module is always registered so code can probe `metrics.enabled` and
// degrade gracefully; when built without PV_METRICS the query functions return
// empty/None and report() prints a one-line "disabled" notice.

#include "pv_metrics.hpp"

#include <string.h>

extern "C" {
  #include "py/runtime.h"
  #include "py/obj.h"
  #include "py/objstr.h"
}

#if PV_METRICS
// Definition of the storage declared in pv_metrics.hpp (BSS, zero-init).
pv_metric_t pv_metrics[PV_METRIC_COUNT];
#endif

extern "C" {

static mp_obj_t pv_metrics_reset(void) {
#if PV_METRICS
  for (size_t i = 0; i < PV_METRIC_COUNT; i++) {
    pv_metrics[i].calls = 0;
    pv_metrics[i].usec = 0;
  }
#endif
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pv_metrics_reset_obj, pv_metrics_reset);

static mp_obj_t pv_metrics_get(mp_obj_t name_in) {
#if PV_METRICS
  const char *name = mp_obj_str_get_str(name_in);
  for (size_t i = 0; i < PV_METRIC_COUNT; i++) {
    if (strcmp(name, pv_metric_names[i]) == 0) {
      mp_obj_t pair[2] = {
        mp_obj_new_int_from_uint(pv_metrics[i].calls),
        mp_obj_new_int_from_ull(pv_metrics[i].usec),
      };
      return mp_obj_new_tuple(2, pair);
    }
  }
#else
  (void)name_in;
#endif
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pv_metrics_get_obj, pv_metrics_get);

static mp_obj_t pv_metrics_all(void) {
  mp_obj_t dict = mp_obj_new_dict(0);
#if PV_METRICS
  for (size_t i = 0; i < PV_METRIC_COUNT; i++) {
    if (pv_metrics[i].calls == 0) continue;  // only report touched bindings
    mp_obj_t pair[2] = {
      mp_obj_new_int_from_uint(pv_metrics[i].calls),
      mp_obj_new_int_from_ull(pv_metrics[i].usec),
    };
    mp_obj_dict_store(dict,
                      mp_obj_new_str(pv_metric_names[i], strlen(pv_metric_names[i])),
                      mp_obj_new_tuple(2, pair));
  }
#endif
  return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pv_metrics_all_obj, pv_metrics_all);

static mp_obj_t pv_metrics_report(void) {
#if PV_METRICS
  // Sort indices by total time, descending (simple insertion sort; the table
  // is small and this is a diagnostic path, not a hot loop).
  uint16_t order[PV_METRIC_COUNT];
  size_t n = 0;
  for (size_t i = 0; i < PV_METRIC_COUNT; i++) {
    if (pv_metrics[i].calls == 0) continue;
    size_t j = n++;
    while (j > 0 && pv_metrics[order[j - 1]].usec < pv_metrics[i].usec) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = (uint16_t)i;
  }

  // Grand total first, so each row can show its share of overall time.
  uint64_t grand = 0;
  uint32_t total_calls = 0;
  for (size_t k = 0; k < n; k++) {
    grand += pv_metrics[order[k]].usec;
    total_calls += pv_metrics[order[k]].calls;
  }

  mp_printf(&mp_plat_print, "%-28s %8s %10s %7s %9s\n",
            "binding", "calls", "total_ms", "pct", "us/call");
  for (size_t k = 0; k < n; k++) {
    uint16_t i = order[k];
    uint32_t calls = pv_metrics[i].calls;
    uint64_t usec = pv_metrics[i].usec;
    // share of total time, in tenths of a percent (rounded), printed as NN.N%
    uint32_t pct10 = grand ? (uint32_t)((usec * 1000 + grand / 2) / grand) : 0;
    mp_printf(&mp_plat_print, "%-28s %8lu %10lu %4lu.%lu%% %9lu\n",
              pv_metric_names[i],
              (unsigned long)calls,
              (unsigned long)(usec / 1000),
              (unsigned long)(pct10 / 10), (unsigned long)(pct10 % 10),
              (unsigned long)(calls ? usec / calls : 0));
  }
  mp_printf(&mp_plat_print, "%-28s %8lu %10lu %4lu.%lu%%\n",
            "(total)", (unsigned long)total_calls,
            (unsigned long)(grand / 1000), 100UL, 0UL);
#else
  mp_printf(&mp_plat_print,
            "picovector metrics disabled (rebuild with -DPV_METRICS=1)\n");
#endif
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pv_metrics_report_obj, pv_metrics_report);

}  // extern "C"

static const mp_rom_map_elem_t pv_metrics_globals_table[] = {
  { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_metrics) },
  { MP_ROM_QSTR(MP_QSTR_reset),    MP_ROM_PTR(&pv_metrics_reset_obj) },
  { MP_ROM_QSTR(MP_QSTR_get),      MP_ROM_PTR(&pv_metrics_get_obj) },
  { MP_ROM_QSTR(MP_QSTR_all),      MP_ROM_PTR(&pv_metrics_all_obj) },
  { MP_ROM_QSTR(MP_QSTR_report),   MP_ROM_PTR(&pv_metrics_report_obj) },
#if PV_METRICS
  { MP_ROM_QSTR(MP_QSTR_enabled),  MP_ROM_TRUE },
#else
  { MP_ROM_QSTR(MP_QSTR_enabled),  MP_ROM_FALSE },
#endif
};
static MP_DEFINE_CONST_DICT(pv_metrics_globals, pv_metrics_globals_table);

extern "C" const mp_obj_module_t modpicovector_metrics = {
  .base = { &mp_type_module },
  .globals = (mp_obj_dict_t *)&pv_metrics_globals,
};
