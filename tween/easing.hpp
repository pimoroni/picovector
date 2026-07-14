#pragma once

#include <stdint.h>

// Easing functions: reshape a normalised time t in [0, 1] into an eased
// progress value. Most stay within [0, 1]; the back/elastic families
// deliberately overshoot outside that range. These are pure, host-clean and
// depend on nothing but <cmath>, so they build and unit-test off-device.

namespace picovector {

  // A plain function pointer so a tween can store its easing as data.
  using easing_fn_t = float (*)(float);

  // The standard (Penner) easing set. Kept as a compact enum so tweens and,
  // later, the MicroPython bindings can refer to an easing by name/value.
  enum class easing_t : uint8_t {
    linear = 0,

    quad_in,    quad_out,    quad_inout,
    cubic_in,   cubic_out,   cubic_inout,
    quart_in,   quart_out,   quart_inout,
    quint_in,   quint_out,   quint_inout,
    sine_in,    sine_out,    sine_inout,
    expo_in,    expo_out,    expo_inout,
    circ_in,    circ_out,    circ_inout,
    back_in,    back_out,    back_inout,
    elastic_in, elastic_out, elastic_inout,
    bounce_in,  bounce_out,  bounce_inout,

    _count
  };

  // Map an easing_t to its function pointer. Never returns nullptr (an unknown
  // value falls back to linear).
  easing_fn_t easing(easing_t e);

  // The individual curves, exposed for direct use. All take and return floats.
  float ease_linear(float t);

  float ease_quad_in(float t);    float ease_quad_out(float t);    float ease_quad_inout(float t);
  float ease_cubic_in(float t);   float ease_cubic_out(float t);   float ease_cubic_inout(float t);
  float ease_quart_in(float t);   float ease_quart_out(float t);   float ease_quart_inout(float t);
  float ease_quint_in(float t);   float ease_quint_out(float t);   float ease_quint_inout(float t);
  float ease_sine_in(float t);    float ease_sine_out(float t);    float ease_sine_inout(float t);
  float ease_expo_in(float t);    float ease_expo_out(float t);    float ease_expo_inout(float t);
  float ease_circ_in(float t);    float ease_circ_out(float t);    float ease_circ_inout(float t);
  float ease_back_in(float t);    float ease_back_out(float t);    float ease_back_inout(float t);
  float ease_elastic_in(float t); float ease_elastic_out(float t); float ease_elastic_inout(float t);
  float ease_bounce_in(float t);  float ease_bounce_out(float t);  float ease_bounce_inout(float t);

}
