#include "picovector.hpp"
#include "primitive.hpp"

namespace picovector {

  // Sides for a full turn of a curve at the given radius. A fixed count is
  // smooth on small shapes and visibly faceted on large ones, so scale it with
  // the circumference (see PV_CURVE_CHORD_PX) and clamp to the configured range.
  static int curve_sides(float radius) {
    float sides = (PV_PI * 2.0f * fabsf(radius)) / PV_CURVE_CHORD_PX;
    if(sides <= PV_CURVE_MIN_SIDES) return PV_CURVE_MIN_SIDES;
    if(sides >= PV_CURVE_MAX_SIDES) return PV_CURVE_MAX_SIDES;
    return (int)ceilf(sides);
  }

  // Sides for a partial sweep of `delta` degrees, at the same density.
  static int curve_steps(float radius, float delta) {
    int steps = (int)ceilf((float)curve_sides(radius) * (delta / 360.0f));
    return steps < 1 ? 1 : steps; // guard against divide-by-zero for small/zero sweeps
  }

  // Walks a (cos, sin) pair around the circle in fixed angular steps, so a curve
  // costs four trig calls to set up plus four multiplies per point, in place of a
  // sinf/cosf pair per point. Float error accumulates: 0.0005px at 120 points and
  // radius 150, 0.01px at 1000 points and radius 1000.
  struct rotor_t {
    float c, s;           // cos/sin of the current angle
    float step_c, step_s; // cos/sin of the step

    rotor_t(float start, float step) :
      c(cosf(start)), s(sinf(start)), step_c(cosf(step)), step_s(sinf(step)) {}

    void advance() {
      float next_c = c * step_c - s * step_s;
      s = s * step_c + c * step_s;
      c = next_c;
    }
  };

  shape_t* regular_polygon(float x, float y, float sides, float radius) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(sides);
    rotor_t vertex(0.0f, (PV_PI * 2.0f) / sides);
    for(int i = 0; i < sides; i++) {
      poly.add_point(vertex.s * radius + x, vertex.c * radius + y);
      vertex.advance();
    }
    result->add_path(poly);
    return result;
  }

  shape_t* circle(float x, float y, float radius) {
    return regular_polygon(x, y, curve_sides(radius), radius);
  }

  shape_t* ellipse(float x, float y, float x_radius, float y_radius) {
    int sides = curve_sides(fmaxf(fabsf(x_radius), fabsf(y_radius)));
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(sides);
    rotor_t vertex(0.0f, (PV_PI * 2.0f) / (float)sides);
    for(int i = 0; i < sides; i++) {
      poly.add_point(vertex.s * x_radius + x, vertex.c * y_radius + y);
      vertex.advance();
    }
    result->add_path(poly);
    return result;
  }

  shape_t* rectangle(float x, float y, float w, float h) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(4);
    poly.add_point(x, y);
    poly.add_point(x + w, y);
    poly.add_point(x + w, y + h);
    poly.add_point(x, y + h);
    result->add_path(poly);
    return result;
  }

  void _build_rounded_rectangle_corner(path_t* path, float x, float y, float r, int q) {
    float quality = 5; // higher the number, lower the quality - selected by experiment
    int steps = ceilf(r / quality) + 1;
    float delta = -(PV_PI / 2) / float(steps);
    float theta = (PV_PI / 2) * q; // select start theta for this quadrant
    for(int i = 0; i <= steps; i++) {
      float xo = sinf(theta) * r, yo = cosf(theta) * r;
      path->add_point(x + xo, y + yo);
      theta += delta;
    }
  }

  shape_t* rounded_rectangle(float x, float y, float w, float h, float r1, float r2, float r3, float r4) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(4);

    // render corners (either hard if radius == 0 or calculate rounded corner vec2s)
    r1 == 0 ? poly.add_point(x    , y    ) : _build_rounded_rectangle_corner(&poly, x + 0 + r1, y + 0 + r1, r1, 3);
    r2 == 0 ? poly.add_point(x + w, y    ) : _build_rounded_rectangle_corner(&poly, x + w - r2, y + 0 + r2, r2, 2);
    r3 == 0 ? poly.add_point(x + w, y + h) : _build_rounded_rectangle_corner(&poly, x + w - r3, y + h - r3, r3, 1);
    r4 == 0 ? poly.add_point(x    , y + h) : _build_rounded_rectangle_corner(&poly, x + 0 + r4, y + h - r4, r4, 0);

    result->add_path(poly);
    return result;
  }

  shape_t* squircle(float x, float y, float size, float n) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);

    const int vec2s = curve_sides(size);
    path_t poly(vec2s);
    // Not a rotor_t: powf() is steepest where its argument approaches zero, so it
    // amplifies the rotor's drift into a visible offset near the axis crossings.
    for(int i = 0; i < vec2s; i++) {
      float t = (PV_PI * 2.0f) * (float)(vec2s - i) / (float)vec2s;
      float ct = cosf(t);
      float st = sinf(t);

      poly.add_point(
        x + copysignf(powf(fabsf(ct), 2.0f / n), ct) * size,
        y + copysignf(powf(fabsf(st), 2.0f / n), st) * size
      );
    }
    result->add_path(poly);
    return result;
  }

  shape_t* arc(float x, float y, float from, float to, float inner, float outer) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);

    // Apply the -90 degree offset equally so the sweep starts at the top.
    // Note: don't fmod each endpoint independently - that corrupts the sweep
    // for arcs crossing 0 degrees and collapses the full-circle case to zero.
    from -= 90.0f;
    to -= 90.0f;
    float delta = fabsf(to - from);
    int steps = curve_steps(fmaxf(fabsf(inner), fabsf(outer)), delta);
    from *= (PV_PI / 180.0f);
    to *= (PV_PI / 180.0f);

    path_t outline((steps + 1) * 2);

    float astep = (to - from) / (float)steps;

    rotor_t outer_edge(from, astep);
    for(int i = 0; i <= steps; i++) {
      outline.add_point(outer_edge.c * outer + x, outer_edge.s * outer + y);
      outer_edge.advance();
    }

    rotor_t inner_edge(to, -astep); // back along the inner radius to close the band
    for(int i = 0; i <= steps; i++) {
      outline.add_point(inner_edge.c * inner + x, inner_edge.s * inner + y);
      inner_edge.advance();
    }

    result->add_path(outline);

    return result;
  }

  shape_t* pie(float x, float y, float from, float to, float radius) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);

    // Apply the -90 degree offset equally so the sweep starts at the top.
    // Note: don't fmod each endpoint independently - that corrupts the sweep
    // for arcs crossing 0 degrees and collapses the full-circle case to zero.
    from -= 90.0f;
    to -= 90.0f;
    float delta = fabsf(to - from);
    int steps = curve_steps(radius, delta);
    from *= (PV_PI / 180.0f);
    to *= (PV_PI / 180.0f);

    path_t outline(steps + 2);

    float astep = (to - from) / (float)steps;

    rotor_t edge(from, astep);
    for(int i = 0; i <= steps; i++) {
      outline.add_point(edge.c * radius + x, edge.s * radius + y);
      edge.advance();
    }

    outline.add_point(x, y);

    result->add_path(outline);

    return result;
  }

  shape_t* star(float x, float y, int spikes, float outer_radius, float inner_radius) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(spikes * 2);
    for(int i = 0; i < spikes * 2; i++) {
      float step = ((PV_PI * 2) / (float)(spikes * 2)) * (float)i;
      float r = i % 2 == 0 ? outer_radius : inner_radius;
      poly.add_point(sinf(step) * r + x, cosf(step) * r + y);
    }
    result->add_path(poly);
    return result;
  }

  shape_t* line(float x1, float y1, float x2, float y2, float w) {
    shape_t* result = new(PV_MALLOC(sizeof(shape_t))) shape_t(1);
    path_t poly(4);

    float dx = x2 - x1;
    float dy = y2 - y1;
    float m = sqrtf(dx * dx + dy * dy);
    if(m == 0.0f) m = 1.0f; // coincident endpoints: avoid NaN, collapse to a point
    dx /= m;
    dy /= m;
    float hw = w / 2.0f;

    poly.add_point(x1 + (dy * hw), y1 - (dx * hw));
    poly.add_point(x2 + (dy * hw), y2 - (dx * hw));
    poly.add_point(x2 - (dy * hw), y2 + (dx * hw));
    poly.add_point(x1 - (dy * hw), y1 + (dx * hw));
    result->add_path(poly);

    return result;
  }
}
