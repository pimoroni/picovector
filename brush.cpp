#include "types.hpp"
#include "blend.hpp"

#include "brush.hpp"

namespace picovector {


  // Blend the shared span buffer with `brush` in one call - dispatches to the
  // brush's batch func. Draw methods call this after filling the buffer.
  void _blend_spans(image_t *target, brush_t *brush) {
    if(brush) brush->blend_spans()(target, brush);
  }
  void _blend_masked_spans(image_t *target, brush_t *brush) {
    if(brush) brush->blend_masked_spans()(target, brush);
  }

}