#include "easing.hpp"

#include <cmath>

#include "../util.hpp" // PV_PI

namespace picovector {

  float ease_linear(float t) { return t; }

  // --- polynomial families (quad/cubic/quart/quint) ------------------------
  // in: t^n ; out: mirror of in ; inout: in over the first half, out over the
  // second. Written out per-power to avoid a runtime powf on the hot path.

  float ease_quad_in(float t)  { return t * t; }
  float ease_quad_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
  float ease_quad_inout(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
  }

  float ease_cubic_in(float t)  { return t * t * t; }
  float ease_cubic_out(float t) { float f = 1.0f - t; return 1.0f - f * f * f; }
  float ease_cubic_inout(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
  }

  float ease_quart_in(float t)  { return t * t * t * t; }
  float ease_quart_out(float t) { float f = 1.0f - t; return 1.0f - f * f * f * f; }
  float ease_quart_inout(float t) {
    return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;
  }

  float ease_quint_in(float t)  { return t * t * t * t * t; }
  float ease_quint_out(float t) { float f = 1.0f - t; return 1.0f - f * f * f * f * f; }
  float ease_quint_inout(float t) {
    return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 5.0f) / 2.0f;
  }

  // --- sine ----------------------------------------------------------------
  float ease_sine_in(float t)  { return 1.0f - cosf((t * PV_PI) / 2.0f); }
  float ease_sine_out(float t) { return sinf((t * PV_PI) / 2.0f); }
  float ease_sine_inout(float t) { return -(cosf(PV_PI * t) - 1.0f) / 2.0f; }

  // --- exponential ---------------------------------------------------------
  float ease_expo_in(float t)  { return t <= 0.0f ? 0.0f : powf(2.0f, 10.0f * t - 10.0f); }
  float ease_expo_out(float t) { return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t); }
  float ease_expo_inout(float t) {
    if(t <= 0.0f) return 0.0f;
    if(t >= 1.0f) return 1.0f;
    return t < 0.5f ? powf(2.0f, 20.0f * t - 10.0f) / 2.0f
                    : (2.0f - powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;
  }

  // --- circular ------------------------------------------------------------
  float ease_circ_in(float t)  { return 1.0f - sqrtf(1.0f - t * t); }
  float ease_circ_out(float t) { return sqrtf(1.0f - (t - 1.0f) * (t - 1.0f)); }
  float ease_circ_inout(float t) {
    return t < 0.5f
      ? (1.0f - sqrtf(1.0f - powf(2.0f * t, 2.0f))) / 2.0f
      : (sqrtf(1.0f - powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
  }

  // --- back (overshoots) ---------------------------------------------------
  static constexpr float BACK_C1 = 1.70158f;
  static constexpr float BACK_C2 = BACK_C1 * 1.525f;
  static constexpr float BACK_C3 = BACK_C1 + 1.0f;

  float ease_back_in(float t)  { return BACK_C3 * t * t * t - BACK_C1 * t * t; }
  float ease_back_out(float t) {
    float f = t - 1.0f;
    return 1.0f + BACK_C3 * f * f * f + BACK_C1 * f * f;
  }
  float ease_back_inout(float t) {
    return t < 0.5f
      ? (powf(2.0f * t, 2.0f) * ((BACK_C2 + 1.0f) * 2.0f * t - BACK_C2)) / 2.0f
      : (powf(2.0f * t - 2.0f, 2.0f) * ((BACK_C2 + 1.0f) * (t * 2.0f - 2.0f) + BACK_C2) + 2.0f) / 2.0f;
  }

  // --- elastic (springy overshoot) -----------------------------------------
  float ease_elastic_in(float t) {
    if(t <= 0.0f) return 0.0f;
    if(t >= 1.0f) return 1.0f;
    const float c4 = (2.0f * PV_PI) / 3.0f;
    return -powf(2.0f, 10.0f * t - 10.0f) * sinf((t * 10.0f - 10.75f) * c4);
  }
  float ease_elastic_out(float t) {
    if(t <= 0.0f) return 0.0f;
    if(t >= 1.0f) return 1.0f;
    const float c4 = (2.0f * PV_PI) / 3.0f;
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
  }
  float ease_elastic_inout(float t) {
    if(t <= 0.0f) return 0.0f;
    if(t >= 1.0f) return 1.0f;
    const float c5 = (2.0f * PV_PI) / 4.5f;
    return t < 0.5f
      ? -(powf(2.0f, 20.0f * t - 10.0f) * sinf((20.0f * t - 11.125f) * c5)) / 2.0f
      :  (powf(2.0f, -20.0f * t + 10.0f) * sinf((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
  }

  // --- bounce --------------------------------------------------------------
  float ease_bounce_out(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;
    if(t < 1.0f / d1)      { return n1 * t * t; }
    else if(t < 2.0f / d1) { t -= 1.5f / d1;   return n1 * t * t + 0.75f; }
    else if(t < 2.5f / d1) { t -= 2.25f / d1;  return n1 * t * t + 0.9375f; }
    else                   { t -= 2.625f / d1; return n1 * t * t + 0.984375f; }
  }
  float ease_bounce_in(float t) { return 1.0f - ease_bounce_out(1.0f - t); }
  float ease_bounce_inout(float t) {
    return t < 0.5f
      ? (1.0f - ease_bounce_out(1.0f - 2.0f * t)) / 2.0f
      : (1.0f + ease_bounce_out(2.0f * t - 1.0f)) / 2.0f;
  }

  easing_fn_t easing(easing_t e) {
    switch(e) {
      case easing_t::linear:        return ease_linear;
      case easing_t::quad_in:       return ease_quad_in;
      case easing_t::quad_out:      return ease_quad_out;
      case easing_t::quad_inout:    return ease_quad_inout;
      case easing_t::cubic_in:      return ease_cubic_in;
      case easing_t::cubic_out:     return ease_cubic_out;
      case easing_t::cubic_inout:   return ease_cubic_inout;
      case easing_t::quart_in:      return ease_quart_in;
      case easing_t::quart_out:     return ease_quart_out;
      case easing_t::quart_inout:   return ease_quart_inout;
      case easing_t::quint_in:      return ease_quint_in;
      case easing_t::quint_out:     return ease_quint_out;
      case easing_t::quint_inout:   return ease_quint_inout;
      case easing_t::sine_in:       return ease_sine_in;
      case easing_t::sine_out:      return ease_sine_out;
      case easing_t::sine_inout:    return ease_sine_inout;
      case easing_t::expo_in:       return ease_expo_in;
      case easing_t::expo_out:      return ease_expo_out;
      case easing_t::expo_inout:    return ease_expo_inout;
      case easing_t::circ_in:       return ease_circ_in;
      case easing_t::circ_out:      return ease_circ_out;
      case easing_t::circ_inout:    return ease_circ_inout;
      case easing_t::back_in:       return ease_back_in;
      case easing_t::back_out:      return ease_back_out;
      case easing_t::back_inout:    return ease_back_inout;
      case easing_t::elastic_in:    return ease_elastic_in;
      case easing_t::elastic_out:   return ease_elastic_out;
      case easing_t::elastic_inout: return ease_elastic_inout;
      case easing_t::bounce_in:     return ease_bounce_in;
      case easing_t::bounce_out:    return ease_bounce_out;
      case easing_t::bounce_inout:  return ease_bounce_inout;
      default:                      return ease_linear;
    }
  }

}
