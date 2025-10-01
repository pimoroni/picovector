#include "picovector.hpp"
#include "primitive.hpp"

namespace picovector {

  shape* regular_polygon(float x, float y, float sides, float radius) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);
    path poly(sides);
    for(int i = 0; i < sides; i++) {
      float theta = ((M_PI * 2.0f) / (float)sides) * (float)i;
      poly.add_point(sin(theta) * radius + x, cos(theta) * radius + y);
    }
    result->add_path(poly);
    return result;
  }

  shape* circle(float x, float y, float radius) {
    int sides = 32;
    return regular_polygon(x, y, sides, radius);
  }

  shape* rectangle(float x1, float y1, float x2, float y2) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);
    path poly(4);
    poly.add_point(x1, y2);
    poly.add_point(x2, y2);
    poly.add_point(x2, y1);
    poly.add_point(x1, y1);
    result->add_path(poly);
    return result;
  }

  // void _ppp_rrect_corner(pp_path_t *path, PP_COORD_TYPE cx, PP_COORD_TYPE cy, PP_COORD_TYPE r, int q) {
  //   float quality = 5; // higher the number, lower the quality - selected by experiment
  //   int steps = ceil(r / quality) + 2; // + 2 to include start and end
  //   float delta = -(M_PI / 2) / steps;
  //   float theta = (M_PI / 2) * q; // select start theta for this quadrant
  //   for(int i = 0; i <= steps; i++) {
  //     PP_COORD_TYPE xo = sin(theta) * r, yo = cos(theta) * r;
  //     pp_path_add_point(path, (pp_point_t){cx + xo, cy + yo});
  //     theta += delta;
  //   }
  // }

  // void _ppp_rrect_path(pp_path_t *path, ppp_rect_def d) {
  //   d.r1 == 0 ? pp_path_add_point(path, (pp_point_t){d.x, d.y})             : _ppp_rrect_corner(path, d.x + d.r1, d.y + d.r1, d.r1, 3);
  //   d.r2 == 0 ? pp_path_add_point(path, (pp_point_t){d.x + d.w, d.y})       : _ppp_rrect_corner(path, d.x + d.w - d.r2, d.y + d.r2, d.r2, 2);
  //   d.r3 == 0 ? pp_path_add_point(path, (pp_point_t){d.x + d.w, d.y + d.h}) : _ppp_rrect_corner(path, d.x + d.w - d.r3, d.y + d.h - d.r3, d.r3, 1);
  //   d.r4 == 0 ? pp_path_add_point(path, (pp_point_t){d.x, d.y + d.h})       : _ppp_rrect_corner(path, d.x + d.r4, d.y + d.h - d.r4, d.r4, 0);
  // }

  // pp_poly_t* ppp_rect(ppp_rect_def d) {
  //   pp_poly_t *shape = pp_poly_new();
  //   pp_path_t *path = pp_poly_add_path(shape);
  //   if(d.r1 == 0.0f && d.r2 ==  0.0f && d.r3 ==  0.0f && d.r4 == 0.0f) { // non rounded rect
  //     pp_point_t points[] = {{d.x, d.y}, {d.x + d.w, d.y}, {d.x + d.w, d.y + d.h}, {d.x, d.y + d.h}};
  //     pp_path_add_points(path, points, 4);
  //     if(d.s != 0) { // stroked, not filled
  //       d.x += d.s; d.y += d.s; d.w -= 2 * d.s; d.h -= 2 * d.s;
  //       pp_path_t *inner = pp_poly_add_path(shape);
  //         pp_point_t points[] = {{d.x, d.y}, {d.x + d.w, d.y}, {d.x + d.w, d.y + d.h}, {d.x, d.y + d.h}};
  //       pp_path_add_points(inner, points, 4);
  //     }
  //   }else{ // rounded rect
  //     _ppp_rrect_path(path, d);
  //     if(d.s != 0) { // stroked, not filled
  //       d.x += d.s; d.y += d.s; d.w -= 2 * d.s; d.h -= 2 * d.s;
  //       d.r1 = _pp_max(0, d.r1 - d.s);
  //       d.r2 = _pp_max(0, d.r2 - d.s);
  //       d.r3 = _pp_max(0, d.r3 - d.s);
  //       d.r4 = _pp_max(0, d.r4 - d.s);
  //       pp_path_t *inner = pp_poly_add_path(shape);
  //       _ppp_rrect_path(inner, d);
  //     }
  //   }
  //   return shape;
  // }


    // static shape rounded_rectangle(float x1, float y1, float x2, float y2, float r1, float r2, float r3, float r4, float stroke=0.0f) {
    // }

  shape* squircle(float x, float y, float size, float n) {    
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);

    //shape *result = new shape(1);
    constexpr int points = 32;
    path poly(points);
    for(int i = 0; i < points; i++) {
        float t = 2 * M_PI * (points - i) / points;
        float ct = cos(t);
        float st = sin(t);

        poly.add_point(
          x + copysign(pow(abs(ct), 2.0 / n), ct) * size,
          y + copysign(pow(abs(st), 2.0 / n), st) * size
        );
    }
    result->add_path(poly);
    return result;
  }

  shape* arc(float x, float y, float from, float to, float radius) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);

    from = fmod(from, 360.0f);
    to = fmod(to, 360.0f);
    float delta = fabs(to - from);
    int steps = (int)(32.0f * (delta / 360.0f));
    from *= (M_PI / 180.0f);
    to *= (M_PI / 180.0f);

    path outline(steps);

    float astep = (to - from) / (float)steps;
    float a = from;

    for(int i = 0; i <= steps; i++) {
      outline.add_point(sin(a) * radius + x, cos(a) * radius + y);
      a += astep;
    }

    result->add_path(outline);

    return result;
  }

  shape* pie(float x, float y, float from, float to, float radius) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);

    from = fmod(from, 360.0f);
    to = fmod(to, 360.0f);
    float delta = fabs(to - from);
    int steps = (int)(32.0f * (delta / 360.0f));
    from *= (M_PI / 180.0f);
    to *= (M_PI / 180.0f);

    path outline(steps + 1); // TODO: is this right?

    float astep = (to - from) / (float)steps;
    float a = from;

    for(int i = 0; i <= steps; i++) {
      outline.add_point(sin(a) * radius + x, cos(a) * radius + y);
      a += astep;
    }

    outline.add_point(x, y); // + 1 point?

    result->add_path(outline);

    return result;
  }


  shape* star(float x, float y, int spikes, float outer_radius, float inner_radius) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);
    path poly(spikes * 2);
    for(int i = 0; i < spikes * 2; i++) {
      float step = ((M_PI * 2) / (float)(spikes * 2)) * (float)i;
      float r = i % 2 == 0 ? outer_radius : inner_radius;
      poly.add_point(sin(step) * r + x, cos(step) * r + y);
    }
    result->add_path(poly);
    return result;
  }

  shape* line(float x1, float y1, float x2, float y2) {
    shape *result = new(PV_MALLOC(sizeof(shape))) shape(1);
    path poly(2);

    poly.add_point(x1, y1);
    poly.add_point(x2, y2);
    result->add_path(poly);

    return result;
  }
}