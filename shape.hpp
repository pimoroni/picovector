#pragma once

#include <vector>
#include "picovector.hpp"
#include "mat3.hpp"
#include "types.hpp"

namespace picovector {

  // Flags for stroke(): OR one value from each group together. Every default is
  // the 0 value (outer alignment, closed path, miter join, butt cap). Naming is
  // FEATURE_VALUE throughout.
  enum stroke_flags_t {
    // alignment of the band relative to the outline (bits 0-1)
    ALIGN_OUTER  = 0,       // grow outward (default)
    ALIGN_INNER  = 1,       // grow inward
    ALIGN_CENTER = 2,       // straddle the outline

    // path closure (bit 2)
    PATH_CLOSED  = 0,       // closed loop (default)
    PATH_OPEN    = 1 << 2,  // open polyline (adds caps)

    // line join — how segments meet at a vertex (bits 3-4)
    JOIN_MITER   = 0,       // sharp corner, clamped to the miter limit (default)
    JOIN_ROUND   = 1 << 3,  // rounded arc
    JOIN_BEVEL   = 2 << 3,  // flattened corner

    // line cap — how open ends are drawn (bits 5-6)
    CAP_BUTT     = 0,       // flat, stops at the endpoint (default)
    CAP_ROUND    = 1 << 5,  // semicircle past the endpoint
    CAP_SQUARE   = 2 << 5   // flat, extended half-width past the endpoint
  };

  // field masks for unpacking stroke flags
  static const uint32_t STROKE_ALIGN_MASK = 0x3;
  static const uint32_t STROKE_JOIN_MASK  = 0x3 << 3;
  static const uint32_t STROKE_CAP_MASK   = 0x3 << 5;

  // Offset an edge along its normal, in place.
  void offset_line_segment(vec2_t &s, vec2_t &e, float offset);

  // Where the infinite lines through p1p2 and p3p4 meet. False when they are
  // parallel, coincident, or too near either to solve, leaving `i` untouched.
  bool intersection(vec2_t p1, vec2_t p2, vec2_t p3, vec2_t p4, vec2_t &i);

  class path_t {
  public:
    std::vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> points;

    path_t(int point_count = 0);
    void add_point(const vec2_t &point);
    void add_point(float x, float y);
    void edge_points(int edge, vec2_t &s, vec2_t &e);
    // Twice the shoelace area; the sign is the winding direction, negative for
    // an anticlockwise ring in screen coordinates. Zero for a degenerate path.
    float signed_area() const;
    void reverse();
    std::vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> offset_ring(float offset, bool closed = true, uint32_t join = JOIN_MITER, float miter_limit = 4.0f);
    void stroke(float thickness, uint32_t flags = 0, float miter_limit = 4.0f);
    void inflate(float offset);
  };

  class shape_t {
  public:
    std::vector<path_t, PV_STD_ALLOCATOR<path_t>> paths;
    mat3_t transform;
    brush_t *_brush = nullptr;

    // cached untransformed (local-space) bounds; geometry is usually static, so
    // we compute the bbox of all points once and reuse it
    rect_t _local_bounds;
    bool _local_bounds_valid = false;

    shape_t(int path_count = 0);
    ~shape_t() {
      //debug_printf("shape destructed\n");
    }
    void add_path(path_t path);
    // Copy another shape's contours in, baking its transform into the points.
    // Fill under NON_ZERO to get the union of the two; under EVEN_ODD the
    // overlap punches a hole. All the contours go into one rasteriser batch, so
    // the combined point count is against MAX_EDGES rather than each part.
    void append(const shape_t &other);
    rect_t local_bounds();
    rect_t bounds();
    /*void draw(image &img); // methods should be on image perhaps? with style/brush and transform passed in?*/
    void stroke(float thickness, uint32_t flags = 0, float miter_limit = 4.0f);
    void brush(brush_t *brush);
  };

}