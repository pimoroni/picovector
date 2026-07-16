#pragma once

#include "../config.hpp"   // PV_TICKS (clock source; see config_default.hpp)
#include "easing.hpp"
#include "interpolate.hpp"

// A tween maps progress to an interpolated value between two endpoints, shaped
// by an easing curve. It holds no clock and no playback state — the caller owns
// the progress and reads a value with at().
//
// at()'s argument is measured against the tween's duration:
//   - default duration is 1, so at(t) takes a normalised 0..1 fraction
//     (slider, scrollbar, hand-driven progress…)
//   - set a duration in seconds and at(secs) takes elapsed seconds instead
//
// i.e. at(x) evaluates the tween at x / duration. The tween never stores "now",
// so it stays a pure value lookup the caller can hit however they like.
//
// Progress is clamped to [0, 1] on the way in, so values outside [0, duration]
// hold at the endpoints. (The easing curve may still overshoot the endpoints
// *within* the range — that's the back/elastic families doing their job.)
//
// Templated over T: anything pv_lerp() accepts — float, int32_t, vec2_t,
// rect_t, xform_t.
//
// The optional start()/now()/done() helpers add a saved start time so a tween
// can work out its own elapsed time. They read the current time from the
// PV_TICKS macro (see config_default.hpp) rather than any platform API, so the
// core stays host-clean. Time is in whatever unit you use for durations; on the
// badge PV_TICKS is milliseconds, so express durations in milliseconds too.

namespace picovector {

  template<typename T>
  class tween_t {
  public:
    tween_t() {}

    // Normalised form: duration defaults to 1, so at() takes a 0..1 fraction.
    tween_t(T from, T to, easing_t e = easing_t::linear)
      : _from(from), _to(to), _ease(easing(e)) {}

    tween_t(T from, T to, easing_fn_t e)
      : _from(from), _to(to), _ease(e ? e : ease_linear) {}

    // Timed form: "tween from -> to over `duration` seconds".
    tween_t(T from, T to, float duration, easing_t e = easing_t::linear)
      : _from(from), _to(to), _duration(duration), _ease(easing(e)) {}

    tween_t(T from, T to, float duration, easing_fn_t e)
      : _from(from), _to(to), _duration(duration), _ease(e ? e : ease_linear) {}

    // --- configuration (chainable) -----------------------------------------
    tween_t &from(T v)                   { _from = v; return *this; }
    tween_t &to(T v)                     { _to = v; return *this; }
    tween_t &duration(float seconds)     { _duration = seconds; return *this; }
    tween_t &easing_curve(easing_t e)    { _ease = easing(e); return *this; }
    tween_t &easing_curve(easing_fn_t f) { _ease = f ? f : ease_linear; return *this; }

    // --- evaluation --------------------------------------------------------
    // Value at progress `t`, measured against the duration: a normalised 0..1
    // fraction by default, or elapsed seconds if a duration was set. Clamped to
    // the [0, 1] range, then eased.
    T at(float t) const {
      float p = _duration > 0.0f ? t / _duration : (t > 0.0f ? 1.0f : 0.0f);
      if(p <= 0.0f) return _from;
      if(p >= 1.0f) return _to;
      return pv_lerp(_from, _to, _ease(p));
    }

    T operator()(float t) const { return at(t); }

    // --- optional self-timing (reads the PV_TICKS clock) -------------------
    // Save the current time (PV_TICKS) as the start time and begin running.
    tween_t &start()        { _start = (float)(PV_TICKS); _running = true; return *this; }
    // Same, but with an explicit start time (e.g. a captured tick value).
    tween_t &start(float t) { _start = t; _running = true; return *this; }
    void     stop()         { _running = false; }

    // Time elapsed since start() (0 before the first start()).
    float elapsed() const { return _running ? (float)(PV_TICKS) - _start : 0.0f; }

    // Value at the current clock time.
    T now() const { return at(elapsed()); }

    // True once the duration has elapsed since start(). False until started.
    bool done() const { return _running && elapsed() >= _duration; }
    bool running() const { return _running; }

    T     from() const     { return _from; }
    T     to() const       { return _to; }
    float duration() const { return _duration; }

  private:
    T     _from{};
    T     _to{};
    float _duration = 1.0f;
    float _start    = 0.0f;
    bool  _running  = false;
    easing_fn_t _ease = ease_linear;
  };

}
