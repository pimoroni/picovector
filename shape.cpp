#include <float.h>
#include <cmath>
#include "shape.hpp"

using std::vector;

namespace picovector {

  // Two edges are treated as parallel below this |sin t|. An order of magnitude
  // above FLT_EPSILON (1.2e-7), so it clears the rounding noise that separates
  // two edges that ought to be exactly collinear, and far below any turn worth
  // drawing: 1e-6 is six ten-thousandths of a degree.
  static constexpr float PV_PARALLEL_EPSILON = 1e-6f;

  void offset_line_segment(vec2_t &s, vec2_t &e, float offset) {
    // calculate normal of edge
    float nx = -(e.y - s.y);
    float ny = e.x - s.x;
    float l = sqrtf(nx * nx + ny * ny);
    // A repeated point is a zero-length edge, which has no direction to take a
    // normal from. Leaving it where it is keeps the degenerate vertex on the
    // outline; dividing by zero put a NaN into the ring, and one NaN point
    // takes the whole path's bounds with it. stroke() guards its cap
    // directions the same way.
    if(l == 0.0f) return;
    nx /= l;
    ny /= l;

    // scale normal to requested offset
    float ox = nx * offset;
    float oy = ny * offset;

    // offset supplied edge vec2s
    s.x += ox;
    s.y += oy;
    e.x += ox;
    e.y += oy;
  }

  // Where the infinite lines through p1p2 and p3p4 meet. False when they are
  // parallel, coincident, or too close to either to solve, leaving `i` untouched.
  //
  // Solved relative to p1. The line constants are a*x + b*y over the points'
  // own coordinates, so a shape a few hundred pixels from the origin carries
  // three leading digits into both terms of the numerator, where they cancel.
  // With edges a fraction of a pixel long there is nothing left underneath: two
  // offset edges of a straight run would meet a couple of thousand pixels away,
  // and whether they did depended on where on screen the run had been placed.
  // Shifting the origin to p1 costs two subtractions and makes the solve depend
  // only on the local geometry.
  bool intersection(vec2_t p1, vec2_t p2, vec2_t p3, vec2_t p4, vec2_t &i) {
    float x2 = p2.x - p1.x, y2 = p2.y - p1.y;
    float x3 = p3.x - p1.x, y3 = p3.y - p1.y;
    float x4 = p4.x - p1.x, y4 = p4.y - p1.y;

    // p1 is the origin now, so its line constant is zero and drops out.
    float a1 = y2,      b1 = -x2;
    float a2 = y4 - y3, b2 = x3 - x4;
    float c2 = a2 * x3 + b2 * y3;

    float determinant = a1 * b2 - a2 * b1;

    // |determinant| is |e1| |e2| |sin t| between the two edges, so measuring it
    // against the edge lengths asks how parallel they are and nothing else -
    // an absolute threshold would scale with the edges and call a short one
    // parallel. Exact zero is not enough on its own: three collinear points
    // give two edges whose normals differ by an ulp after normalisation, which
    // leaves a determinant at the noise floor rather than at zero, and dividing
    // by it is what produced the runaway geometry.
    float e1 = a1 * a1 + b1 * b1;
    float e2 = a2 * a2 + b2 * b2;
    if(!(determinant * determinant > PV_PARALLEL_EPSILON * PV_PARALLEL_EPSILON * e1 * e2)) {
      return false; // parallel, coincident, or a degenerate (zero-length) edge
    }

    i.x = p1.x + (-b1 * c2) / determinant;
    i.y = p1.y + ( a1 * c2) / determinant;
    return true;
  }



  shape_t::shape_t(int path_count) {
    //debug_printf("shape constructed\n");
    paths.reserve(path_count);
  }

  void shape_t::add_path(path_t path) {
    paths.push_back(path);
    _local_bounds_valid = false;
  }

  void shape_t::append(const shape_t &other) {
    if(other.paths.empty()) return;

    // Winding is normalised per source shape, not per contour: the primitives
    // disagree on direction (circle() one way, rectangle() the other, pie()
    // whichever way its sweep runs), so concatenating two of them can cancel the
    // overlap to a hole under NON_ZERO. Flipping a shape's contours together
    // preserves the relative winding inside it, so a counter-wound hole survives.
    float reference = 0.0f;
    for(const path_t &path : paths) {
      reference = path.signed_area();
      if(reference != 0.0f) break;
    }

    size_t first = paths.size();
    for(const path_t &path : other.paths) {
      path_t copy((int)path.points.size());
      for(const vec2_t &point : path.points) {
        copy.add_point(vec2_t(point).transform(other.transform));
      }
      paths.push_back(copy);
    }
    _local_bounds_valid = false;

    if(reference == 0.0f) return;

    float lead = 0.0f;
    for(size_t i = first; i < paths.size(); i++) {
      lead = paths[i].signed_area();
      if(lead != 0.0f) break;
    }
    if(lead == 0.0f || (lead < 0.0f) == (reference < 0.0f)) return;

    for(size_t i = first; i < paths.size(); i++) paths[i].reverse();
  }

  // untransformed bounding box of all points, cached (geometry rarely changes)
  rect_t shape_t::local_bounds() {
    if(!_local_bounds_valid) {
      float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
      for(const path_t &path : paths) {
        for(const vec2_t &v : path.points) {
          minx = min(minx, v.x);
          miny = min(miny, v.y);
          maxx = max(maxx, v.x);
          maxy = max(maxy, v.y);
        }
      }
      _local_bounds = rect_t(minx, miny, maxx - minx, maxy - miny);
      _local_bounds_valid = true;
    }
    return _local_bounds;
  }

  rect_t shape_t::bounds() {
    // transform only the four corners of the cached local bbox rather than every
    // point. for any affine transform the AABB of the transformed corners
    // encloses the transformed shape (slightly conservative under rotation,
    // which only ever adds a few empty pixels to the tile loop).
    rect_t lb = local_bounds();
    vec2_t c[4] = {
      vec2_t(lb.x,        lb.y       ).transform(&transform),
      vec2_t(lb.x + lb.w, lb.y       ).transform(&transform),
      vec2_t(lb.x,        lb.y + lb.h).transform(&transform),
      vec2_t(lb.x + lb.w, lb.y + lb.h).transform(&transform),
    };

    float minx = min(min(c[0].x, c[1].x), min(c[2].x, c[3].x));
    float miny = min(min(c[0].y, c[1].y), min(c[2].y, c[3].y));
    float maxx = max(max(c[0].x, c[1].x), max(c[2].x, c[3].x));
    float maxy = max(max(c[0].y, c[1].y), max(c[2].y, c[3].y));

    return rect_t(minx, miny, ceilf(maxx) - minx, ceilf(maxy) - miny);
  }

  // these should be methods on image maybe?
  // void shape::draw(image &img) {
  //   if(style) {
  //     render(*this, img, style);
  //   }
  // }

  void shape_t::brush(brush_t *brush) {
    this->_brush = brush;
  }

  void shape_t::stroke(float thickness, uint32_t flags, float miter_limit) {
    for(int i = 0; i < (int)this->paths.size(); i++) {
      this->paths[i].stroke(thickness, flags, miter_limit);
    }
    _local_bounds_valid = false; // points changed, recompute local bbox
  }



  path_t::path_t(int vec2_count) {
    points.reserve(vec2_count);
  }

  void path_t::add_point(const vec2_t &vec2) {
    points.push_back(vec2);
  }

  void path_t::add_point(float x, float y) {
    points.push_back(vec2_t(x, y));
  }

  // Shoelace, summed relative to the first point: a ring a few hundred pixels
  // from the origin otherwise carries three leading digits into every term,
  // where they cancel and take the sign of a thin shape with them.
  float path_t::signed_area() const {
    size_t count = points.size();
    if(count < 3) return 0.0f;
    const vec2_t &o = points[0];
    float area = 0.0f;
    for(size_t i = 0, j = count - 1; i < count; j = i++) {
      area += (points[j].x - o.x) * (points[i].y - o.y) -
              (points[i].x - o.x) * (points[j].y - o.y);
    }
    return area;
  }

  void path_t::reverse() {
    for(size_t i = 0, j = points.size(); i + 1 < j; i++) {
      j--;
      vec2_t t = points[i];
      points[i] = points[j];
      points[j] = t;
    }
  }

  void path_t::edge_points(int edge, vec2_t &s, vec2_t &e) {
    // return the two vec2s that make up an edge
    s = edge == -1 ? points.back() : points[edge];
    e = edge == (int)points.size() - 1 ? points.front() : points[edge + 1];
  }

  // push the interior points of a circular arc from `from` to `to` about
  // `center`, taking the short way round (used for round joins)
  static void add_arc(vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> &ring,
                      const vec2_t &center, const vec2_t &from, const vec2_t &to, float radius) {
    float a0 = atan2f(from.y - center.y, from.x - center.x);
    float a1 = atan2f(to.y - center.y, to.x - center.x);
    float da = a1 - a0;
    while(da >  PV_PI) da -= 2.0f * PV_PI;
    while(da < -PV_PI) da += 2.0f * PV_PI;
    int n = (int)ceilf(fabsf(da) / (PV_PI / 8.0f)); // ~22.5 deg per segment
    for(int s = 1; s < n; s++) {
      float a = a0 + da * (float)s / (float)n;
      ring.push_back(vec2_t(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
    }
  }

  // Build a copy of the outline pushed out (or in, for a negative offset) along
  // each edge normal. At convex corners the offset edges diverge, leaving a gap
  // that the join fills (miter / round / bevel); concave corners just use the
  // offset-edge intersection. This is the per-side boundary the ribbon spans.
  vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> path_t::offset_ring(float offset, bool closed, uint32_t join, float miter_limit) {
    int c = points.size();
    vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> ring;
    ring.reserve(c + 8);

    for(int i = 0; i < c; i++) {
      // open-path endpoints have a single adjacent edge: take its offset
      // endpoint; the cap (if any) is added later by stroke()
      if(!closed && i == 0) {
        vec2_t s, e; edge_points(0, s, e); offset_line_segment(s, e, offset);
        ring.push_back(s); continue;
      }
      if(!closed && i == c - 1) {
        vec2_t s, e; edge_points(c - 2, s, e); offset_line_segment(s, e, offset);
        ring.push_back(e); continue;
      }

      vec2_t p1, p2; edge_points(i - 1, p1, p2); offset_line_segment(p1, p2, offset); // incoming offset edge
      vec2_t p3, p4; edge_points(i, p3, p4);     offset_line_segment(p3, p4, offset); // outgoing offset edge
      const vec2_t &v = points[i];

      // turn direction at this vertex
      int pi = (i - 1 + c) % c, ni = (i + 1) % c;
      float d1x = points[i].x - points[pi].x, d1y = points[i].y - points[pi].y;
      float d2x = points[ni].x - points[i].x, d2y = points[ni].y - points[i].y;
      float cross = d1x * d2y - d1y * d2x;

      // convex (outer) corner: offset edges diverge -> the join lives here.
      bool outer = (cross * offset) < 0.0f;

      if(!outer) {
        // Concave corner: the offset edges cross, and the crossing is the ring
        // point. It lies |offset| / sin(half the interior angle) from the
        // vertex, so the bound the miter limit puts on the outer side holds
        // here too - and beyond it the crossing is rounding noise rather than
        // geometry. Three near-collinear points make both edges very nearly one
        // line, where a crossing thousands of pixels away is as easy to compute
        // as the right answer, which is a hair over |offset| out. The outer side
        // has always rejected those; this one used to keep them, and one
        // straight run of points would stroke to a spike off the screen.
        vec2_t m;
        if(intersection(p1, p2, p3, p4, m)) {
          float mx = m.x - v.x, my = m.y - v.y;
          if(mx * mx + my * my <= miter_limit * miter_limit * offset * offset) {
            ring.push_back(m);
            continue;
          }
        }
        ring.push_back(p2);   // bevel, as the outer side does
        continue;
      }

      if(join == JOIN_MITER) {
        vec2_t m;
        if(intersection(p1, p2, p3, p4, m)) {
          float mx = m.x - v.x, my = m.y - v.y;
          // SVG miter limit: miterLength / strokeWidth == |m - v| / |offset|
          if(mx * mx + my * my <= miter_limit * miter_limit * offset * offset) {
            ring.push_back(m);
            continue;
          }
        }
        ring.push_back(p2); ring.push_back(p3); // limit exceeded (or parallel) -> bevel
      } else if(join == JOIN_ROUND) {
        ring.push_back(p2);
        add_arc(ring, v, p2, p3, fabsf(offset));
        ring.push_back(p3);
      } else { // JOIN_BEVEL
        ring.push_back(p2); ring.push_back(p3);
      }
    }

    return ring;
  }

  // bridge side-boundary endpoints `a` (outer) and `b` (inner) with the cap,
  // pushing the connecting points (b itself comes from the ring that follows)
  static void add_cap(vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> &ring,
                      const vec2_t &a, const vec2_t &b, const vec2_t &dir,
                      uint32_t cap, float half_width) {
    if(cap == CAP_SQUARE) {
      ring.push_back(vec2_t(a.x + dir.x * half_width, a.y + dir.y * half_width));
      ring.push_back(vec2_t(b.x + dir.x * half_width, b.y + dir.y * half_width));
    } else if(cap == CAP_ROUND) {
      // semicircle from a to b bulging along dir (a and b are a diameter apart)
      vec2_t mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
      float ux = a.x - mid.x, uy = a.y - mid.y; // length == half_width
      int n = 8;
      for(int s = 1; s < n; s++) {
        float ang = PV_PI * (float)s / (float)n;
        float ca = cosf(ang), sa = sinf(ang);
        ring.push_back(vec2_t(mid.x + ux * ca + dir.x * half_width * sa,
                              mid.y + uy * ca + dir.y * half_width * sa));
      }
    }
    // CAP_BUTT: nothing, the straight edge a->b is the cap
  }

  void path_t::stroke(float thickness, uint32_t flags, float miter_limit) {
    int c = points.size();
    if(c < 2) { points.clear(); return; }

    // unpack the flags (see stroke_flags_t) — each field stays in its bit range
    uint32_t align = flags & STROKE_ALIGN_MASK;
    bool closed = (flags & PATH_OPEN) == 0;
    uint32_t join = flags & STROKE_JOIN_MASK;
    uint32_t cap = flags & STROKE_CAP_MASK;

    // Pick the two boundary offsets the ribbon spans, relative to the outline.
    float outer_off, inner_off;
    switch(align) {
      case ALIGN_INNER:  outer_off = 0.0f;             inner_off = -thickness;        break;
      case ALIGN_CENTER: outer_off = thickness * 0.5f; inner_off = -thickness * 0.5f; break;
      case ALIGN_OUTER:
      default:           outer_off = thickness;         inner_off = 0.0f;             break;
    }

    // endpoint outward directions for caps (before points is mutated)
    vec2_t start_dir, end_dir;
    {
      float dx = points[0].x - points[1].x, dy = points[0].y - points[1].y;
      float l = sqrtf(dx * dx + dy * dy); if(l == 0.0f) l = 1.0f;
      start_dir = vec2_t(dx / l, dy / l);
      dx = points[c - 1].x - points[c - 2].x; dy = points[c - 1].y - points[c - 2].y;
      l = sqrtf(dx * dx + dy * dy); if(l == 0.0f) l = 1.0f;
      end_dir = vec2_t(dx / l, dy / l);
    }

    // Compute both boundaries from the original outline before mutating points.
    vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> outer = offset_ring(outer_off, closed, join, miter_limit);
    vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> inner = offset_ring(inner_off, closed, join, miter_limit);

    points.clear();

    if(closed) {
      // Emit the two rings as one even-odd filled ribbon (each ring closed by
      // repeating its first point); the inner ring punches out the hole.
      points.insert(points.end(), outer.begin(), outer.end());
      points.push_back(outer.front());
      points.insert(points.end(), inner.begin(), inner.end());
      points.push_back(inner.front());
    } else {
      // Open polyline: one closed band along the outer boundary, an end cap,
      // back along the inner boundary (reversed), then a start cap.
      float half_width = (outer_off - inner_off) * 0.5f;
      points.insert(points.end(), outer.begin(), outer.end());
      add_cap(points, outer.back(), inner.back(), end_dir, cap, half_width);
      for(int i = (int)inner.size() - 1; i >= 0; i--) points.push_back(inner[i]);
      add_cap(points, inner.front(), outer.front(), start_dir, cap, half_width);
    }
  }

  void path_t::inflate(float offset) {
    vector<vec2_t, PV_STD_ALLOCATOR<vec2_t>> new_points(points.size());

    int edge_count = points.size();
    for(int i = 0; i < edge_count; i++) {
      vec2_t p1, p2; // edge 1 start and end
      edge_points(i, p1, p2);
      offset_line_segment(p1, p2, offset);

      vec2_t p3, p4; // edge 2 start and end
      edge_points(i + 1, p3, p4);
      offset_line_segment(p3, p4, offset);

      // Find where the two offset edges meet. Parallel or coincident edges have
      // no intersection - which two collinear segments, or a repeated point, give
      // you - and intersection() leaves `pi` untouched, so taking it regardless
      // stored an uninitialised coordinate. They meet at the shared offset
      // endpoint in that case, which is what offset_ring() bevels to as well.
      vec2_t pi;
      if(!intersection(p1, p2, p3, p4, pi)) pi = p2;
      new_points[i] = pi;
    }

    points = new_points;
  }

}
