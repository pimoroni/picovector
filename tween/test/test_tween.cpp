// Host-side unit tests for the tween module. Build & run:
//   c++ -std=c++17 -I.. test_tween.cpp ../easing.cpp ../tween.cpp -o /tmp/tw && /tmp/tw
//
// Covers easing invariants, pv_lerp across types, xform compose/decompose and
// round-trip, and tween_t playback (timing, delay, easing, loop, yoyo).

#include <cstdio>
#include <cmath>

// Drive the self-timing helpers from a controllable fake clock: define PV_TICKS
// to a mutable global before the tween header pulls in its config default.
static float g_fake_ticks = 0.0f;
#define PV_TICKS g_fake_ticks

#include "../tween.hpp"

using namespace picovector;

static int failures = 0;
static int checks = 0;

static void check(bool cond, const char *what) {
  checks++;
  if(!cond) { failures++; printf("  FAIL: %s\n", what); }
}

static bool approx(float a, float b, float eps = 1e-4f) {
  return fabsf(a - b) <= eps;
}

static void test_easing() {
  printf("easing...\n");
  // Every curve must pin the endpoints: f(0)=0, f(1)=1.
  for(int i = 0; i < (int)easing_t::_count; i++) {
    easing_fn_t f = easing((easing_t)i);
    check(approx(f(0.0f), 0.0f), "ease(0) == 0");
    check(approx(f(1.0f), 1.0f), "ease(1) == 1");
  }
  check(approx(ease_linear(0.5f), 0.5f), "linear midpoint");
  check(ease_quad_in(0.5f) < 0.5f, "quad_in below linear");
  check(ease_quad_out(0.5f) > 0.5f, "quad_out above linear");
  // back/elastic overshoot the [0,1] band somewhere in the interior.
  check(ease_back_in(0.3f) < 0.0f, "back_in undershoots");
  check(ease_back_out(0.7f) > 1.0f, "back_out overshoots");
  // unknown enum falls back to linear (never null)
  check(easing((easing_t)200) == ease_linear, "unknown -> linear");
}

static void test_lerp() {
  printf("pv_lerp...\n");
  check(approx(pv_lerp(0.0f, 10.0f, 0.5f), 5.0f), "float lerp");
  check(pv_lerp(0, 10, 0.5f) == 5, "int lerp mid");
  check(pv_lerp(0, 3, 0.5f) == 2, "int lerp rounds (1.5 -> 2)");

  vec2_t v = pv_lerp(vec2_t(0, 0), vec2_t(4, 8), 0.25f);
  check(approx(v.x, 1.0f) && approx(v.y, 2.0f), "vec2 lerp");

  rect_t r = pv_lerp(rect_t(0, 0, 10, 20), rect_t(10, 10, 30, 40), 0.5f);
  check(approx(r.x, 5) && approx(r.y, 5) && approx(r.w, 20) && approx(r.h, 30), "rect lerp");
}

static void test_xform() {
  printf("xform...\n");
  // Identity transform leaves a point untouched.
  xform_t id;
  vec2_t p(3, 4);
  vec2_t pi = p.transform(id.to_mat3());
  check(approx(pi.x, 3) && approx(pi.y, 4), "identity xform");

  // Pure translate.
  xform_t tr; tr.translate = vec2_t(10, -5);
  vec2_t pt = p.transform(tr.to_mat3());
  check(approx(pt.x, 13) && approx(pt.y, -1), "translate xform");

  // Rotate 90° CCW about an origin: the origin itself is a fixed point.
  xform_t rot; rot.rotation = PV_PI / 2.0f; rot.origin = vec2_t(5, 5);
  vec2_t origin_pt = vec2_t(5, 5).transform(rot.to_mat3());
  check(approx(origin_pt.x, 5) && approx(origin_pt.y, 5), "origin is fixed under rotation");
  // A point to the right of the origin rotates to above it (CCW).
  vec2_t rp = vec2_t(6, 5).transform(rot.to_mat3());
  check(approx(rp.x, 5) && approx(rp.y, 6), "90deg CCW about origin");

  // Scale about an origin keeps the origin fixed.
  xform_t sc; sc.scale = vec2_t(2, 3); sc.origin = vec2_t(1, 1);
  vec2_t sp = vec2_t(2, 2).transform(sc.to_mat3());
  check(approx(sp.x, 3) && approx(sp.y, 4), "scale about origin");

  // Decompose round-trip: compose a TRS matrix, decompose, recompose, compare
  // the effect on a probe point (origin=0 for a clean round-trip).
  xform_t src(vec2_t(7, -2), 0.6f, vec2_t(1.5f, 0.8f));
  mat3_t m = src.to_mat3();
  xform_t dec = xform_t::decompose(m);
  check(approx(dec.translate.x, 7) && approx(dec.translate.y, -2), "decompose translate");
  check(approx(dec.rotation, 0.6f), "decompose rotation");
  check(approx(dec.scale.x, 1.5f) && approx(dec.scale.y, 0.8f), "decompose scale");
  vec2_t a = vec2_t(2, 3).transform(m);
  vec2_t b = vec2_t(2, 3).transform(dec.to_mat3());
  check(approx(a.x, b.x) && approx(a.y, b.y), "decompose round-trip point");

  // Shortest-path rotation lerp: +170deg to -170deg crosses through 180deg.
  xform_t ra; ra.rotation =  170.0f * PV_PI / 180.0f;
  xform_t rb; rb.rotation = -170.0f * PV_PI / 180.0f;
  xform_t mid = pv_lerp(ra, rb, 0.5f);
  check(approx(fabsf(mid.rotation), PV_PI, 1e-3f), "rotation takes short path (~180deg)");
}

static void test_at() {
  printf("at()...\n");
  // Linear float tween: value tracks t directly.
  tween_t<float> tw(0.0f, 100.0f);
  check(approx(tw.at(0.0f), 0.0f), "at(0) == from");
  check(approx(tw.at(0.5f), 50.0f), "at(0.5) halfway");
  check(approx(tw.at(1.0f), 100.0f), "at(1) == to");
  check(approx(tw(0.25f), 25.0f), "operator() works");

  // t clamps outside [0, 1] (holds at the endpoints).
  check(approx(tw.at(-3.0f), 0.0f), "t<0 clamps to from");
  check(approx(tw.at(9.0f), 100.0f), "t>1 clamps to to");

  // Easing shapes the curve but still pins the endpoints.
  tween_t<float> te(0.0f, 100.0f, easing_t::quad_in);
  check(approx(te.at(0.0f), 0.0f) && approx(te.at(1.0f), 100.0f), "eased endpoints pinned");
  check(te.at(0.5f) < 50.0f, "quad_in lags at midpoint");

  // A raw function pointer works too.
  tween_t<float> tf(0.0f, 10.0f, ease_cubic_out);
  check(approx(tf.at(1.0f), 10.0f), "fn-pointer curve endpoint");

  // vec2 / rect flow through the same evaluator.
  tween_t<vec2_t> tv(vec2_t(0, 0), vec2_t(10, 20));
  vec2_t vv = tv.at(0.5f);
  check(approx(vv.x, 5) && approx(vv.y, 10), "vec2 at(0.5)");

  tween_t<rect_t> tr(rect_t(0, 0, 10, 10), rect_t(10, 10, 30, 30));
  rect_t rr = tr.at(0.5f);
  check(approx(rr.x, 5) && approx(rr.w, 20), "rect at(0.5)");

  // An xform tween drives a point in an arc: quarter turn about an origin.
  xform_t start(vec2_t(0, 0), 0.0f, vec2_t(1, 1), vec2_t(5, 5));
  xform_t end  (vec2_t(0, 0), PV_PI / 2.0f, vec2_t(1, 1), vec2_t(5, 5));
  tween_t<xform_t> tx(start, end);
  vec2_t arc = vec2_t(6, 5).transform(tx.at(1.0f).to_mat3());
  check(approx(arc.x, 5) && approx(arc.y, 6), "xform tween arc endpoint");
  // Chainable config.
  tween_t<float> tc;
  tc.from(10.0f).to(20.0f).easing_curve(easing_t::linear);
  check(approx(tc.at(0.5f), 15.0f), "chainable config");

  // Timed form: with a duration, at() takes seconds instead of a 0..1 fraction.
  tween_t<float> tt(0.0f, 100.0f, 2.0f);          // over 2 seconds
  check(approx(tt.duration(), 2.0f), "duration stored");
  check(approx(tt.at(0.0f), 0.0f), "at(0s) == from");
  check(approx(tt.at(1.0f), 50.0f), "at(1s) halfway of 2s");
  check(approx(tt.at(2.0f), 100.0f), "at(dur) == to");
  check(approx(tt.at(5.0f), 100.0f), "at() past end clamps");
  check(approx(tt.at(-1.0f), 0.0f), "at() negative clamps");

  // Chainable duration setter.
  tween_t<float> tcd(0.0f, 10.0f);
  tcd.duration(4.0f);
  check(approx(tcd.at(1.0f), 2.5f), "chainable duration");

  // Zero duration is instant.
  tween_t<float> tz(0.0f, 9.0f, 0.0f);
  check(approx(tz.at(0.0f), 0.0f) && approx(tz.at(0.1f), 9.0f), "zero duration instant");
}

static void test_self_timing() {
  printf("self-timing...\n");
  g_fake_ticks = 0.0f;

  tween_t<float> tw(0.0f, 100.0f, 2.0f);   // over 2 (clock units)
  // Before start(), the helpers are inert.
  check(!tw.running(), "not running before start");
  check(!tw.done(), "not done before start");
  check(approx(tw.now(), 0.0f), "now() == from before start");

  g_fake_ticks = 10.0f;                      // clock can be any offset
  tw.start();                                // saves start = 10
  check(tw.running(), "running after start");
  check(approx(tw.now(), 0.0f), "now() == from at start");
  check(!tw.done(), "not done at start");

  g_fake_ticks = 11.0f;                      // 1 elapsed of 2
  check(approx(tw.elapsed(), 1.0f), "elapsed tracks clock");
  check(approx(tw.now(), 50.0f), "now() halfway");
  check(!tw.done(), "not done halfway");

  g_fake_ticks = 12.0f;                      // duration reached
  check(approx(tw.now(), 100.0f), "now() == to at end");
  check(tw.done(), "done at duration");

  g_fake_ticks = 20.0f;                      // past end
  check(approx(tw.now(), 100.0f), "now() holds past end");
  check(tw.done(), "still done past end");

  // Explicit start time.
  tween_t<vec2_t> tv(vec2_t(0, 0), vec2_t(10, 20), 4.0f);
  tv.start(20.0f);                           // start at t=20
  g_fake_ticks = 22.0f;                       // 2 of 4 -> halfway
  vec2_t v = tv.now();
  check(approx(v.x, 5) && approx(v.y, 10), "explicit-start now()");

  // stop() makes the helpers inert again.
  tv.stop();
  check(!tv.running() && !tv.done(), "stop() halts timing");
}

int main() {
  test_easing();
  test_lerp();
  test_xform();
  test_at();
  test_self_timing();
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
