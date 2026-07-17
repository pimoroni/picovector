#pragma once

// Public interface of the polygon rasteriser (rasteriser.cpp): the retained-mode
// renderer that turns transformed paths into pixel coverage/spans, plus the
// per-frame profiling hook. The types in these signatures are forward-declared in
// picovector.hpp.
#include "picovector.hpp"

namespace picovector {

  // Retained polygon renderer: begin a batch, add transformed paths (each returns
  // the free edge-slot count, or -1 if it would overflow), then flush once.
  void render_begin();
  int  render_add_path(const vec2_t *pts, int count, mat3_t *transform);
  void render_flush(image_t *target, brush_t *brush);

  // Convenience wrapper: rasterise a whole shape in one call.
  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush);

  // Profiling hook — call once per frame (wired into image clear); a no-op unless
  // PV_PROFILE is enabled.
  void pv_profile_frame();

}
