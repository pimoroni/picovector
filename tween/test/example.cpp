// Simple usage example for the tween module. Build & run:
//   c++ -std=c++17 -I.. example.cpp ../easing.cpp ../tween.cpp -o /tmp/ex && /tmp/ex

#include <cstdio>
#include "../tween.hpp"

using namespace picovector;

int main() {
  // 1) float — ease a scalar from 0 -> 100 with a cubic-out curve.
  tween_t<float> fade(0.0f, 100.0f, easing_t::cubic_out);

  // 2) vec2 — slide a point across the screen, overshooting with back-out.
  tween_t<vec2_t> slide(vec2_t(10, 10), vec2_t(230, 120), easing_t::back_out);

  // 3) rect — grow/move a box (position and size interpolate together).
  tween_t<rect_t> grow(rect_t(0, 0, 20, 20), rect_t(60, 40, 120, 80), easing_t::quad_inout);

  // 4) start/end matrices — tween between two transforms. Decompose each mat3
  //    into translate/rotate/scale, interpolate, and recompose. Here: start at
  //    the origin unrotated, end shifted + spun 90deg + doubled in size.
  mat3_t m_start;                                   // identity
  mat3_t m_end;
  m_end.translate(120, 60).rotate(90).scale(2.0f);  // T * R * S

  tween_t<xform_t> move(xform_t::decompose(m_start),
                        xform_t::decompose(m_end),
                        easing_t::sine_inout);

  // A probe point we push through the tweened matrix each step.
  vec2_t probe(10, 0);

  printf(" t   fade    slide            grow                    matrix->probe\n");
  for(int i = 0; i <= 4; i++) {
    float t = i / 4.0f;                 // your own 0..1 progress

    float  f = fade.at(t);
    vec2_t p = slide.at(t);
    rect_t r = grow.at(t);
    mat3_t m = move.at(t).to_mat3();    // recompose the interpolated transform
    vec2_t q = probe.transform(m);      // apply it to a point

    printf("%.2f  %6.2f  (%6.1f,%6.1f)  (%5.1f,%5.1f %5.1f x%5.1f)  (%6.1f,%6.1f)\n",
           t, f, p.x, p.y, r.x, r.y, r.w, r.h, q.x, q.y);
  }
  return 0;
}
