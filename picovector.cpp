#include <algorithm>

// Set to 1 to enable rasteriser phase profiling (prints phase timings to the REPL).
#define PV_PROFILE 0

#if PV_PROFILE
#include <cstdio>
#endif

#include "picovector.hpp"
#include "brush.hpp"
#include "image.hpp"
#include "shape.hpp"
#include "font.hpp"
#include "types.hpp"
#include "mat3.hpp"
#include "blend.hpp"

using std::sort, std::min, std::max;

// memory pool for rasterisation, png decoding, and other memory intensive
// tasks (sized to fit PNGDEC state) - on pico it *must* be 32bit aligned (i
// found out the hard way.)
char __attribute__((aligned(4))) PicoVector_working_buffer[working_buffer_size];

#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define MAX_NODES_PER_SCANLINE 64

#define TILE_BUFFER_SIZE (TILE_WIDTH * (TILE_HEIGHT + 1) * sizeof(uint8_t)) // ~4kB tile buffer
#define NODE_BUFFER_ROW_SIZE (MAX_NODES_PER_SCANLINE * sizeof(int16_t))
#define NODE_BUFFER_SIZE (TILE_HEIGHT * 4 * NODE_BUFFER_ROW_SIZE) // 32kB node buffer
#define NODE_COUNT_BUFFER_SIZE (TILE_HEIGHT * 4 * sizeof(uint8_t)) // 256 byte node count buffer

// buffer that each tile is rendered into before callback
uint8_t *tile_buffer = (uint8_t *)&PicoVector_working_buffer[0];
int16_t *node_buffer = (int16_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE];
uint8_t *node_count_buffer = (uint8_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE + NODE_BUFFER_SIZE];

// --- rasteriser phase profiling (toggle with PV_PROFILE above) ---
#if PV_PROFILE
extern "C" uint64_t time_us_64(void);
extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len); // MicroPython REPL output
static uint64_t pv_us_bounds = 0, pv_us_build = 0, pv_us_raster = 0;
static int pv_calls = 0;
#define PV_PROF_DECL(v) uint64_t v = time_us_64()
#define PV_PROF_ACC(acc, v) (acc) += time_us_64() - (v)
#else
#define PV_PROF_DECL(v)
#define PV_PROF_ACC(acc, v)
#endif
// -----------------------------------------------------------------

static inline void insertion_sort_i16(int16_t* a, int n) {
  for (int i = 1; i < n; ++i) {
    int16_t key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      --j;
    }
    a[j + 1] = key;
  }
}

namespace picovector {

  int sign(int v) {return (v > 0) - (v < 0);}

  void add_line_segment_to_nodes(vec2_t start, vec2_t end, rect_t *tb) {
    // winding direction: downward edges wind +1, upward (swapped) edges -1
    int dir_bit = 0;
    if(end.y < start.y) {
      vec2_t tmp = start; start = end; end = tmp;
      dir_bit = 1;
    }

    if (end.y < 0.0f || start.y > tb->h || end.y == start.y) return;

    float x = start.x;
    float dx = (end.x - start.x) / (end.y - start.y);

    if(start.y < 0.0f) {
      x += fabs(start.y) * dx;
      start.y = 0.0f;
    }

    if(end.y > tb->h) {
      end.y = tb->h;
    }

    int minx = 0;
    int maxx = ceilf(tb->w);

    int sy = int(start.y);
    int ey = int(end.y);

    for(int iy = sy; iy < ey; iy++) {
      int ix = max(min(int(x), maxx), minx);

      // pack: x in the high bits, winding direction in bit 0 (tile-local x fits)
      node_buffer[(iy * MAX_NODES_PER_SCANLINE) + node_count_buffer[iy]] = (ix << 1) | dir_bit;
      node_count_buffer[iy]++;

      x += dx;
    }
  }

  void build_nodes(path_t *path, rect_t *tb, mat3_t *transform, uint aa) {
    const auto &pts = path->points;
    size_t count = pts.size();
    if(count == 0) return;

    vec2_t offset = tb->tl();
    float scale = float(1 << aa);

    // fold the transform, antialias scale, and tile offset into one affine
    // applied per point:  x' = a00*x + a01*y + bx,  y' = a10*x + a11*y + by
    float a00, a01, a10, a11, bx, by;
    if(transform) {
      a00 = transform->v00 * scale; a01 = transform->v01 * scale;
      a10 = transform->v10 * scale; a11 = transform->v11 * scale;
      bx  = transform->v02 * scale - offset.x;
      by  = transform->v12 * scale - offset.y;
    } else {
      a00 = scale; a01 = 0.0f; a10 = 0.0f; a11 = scale;
      bx  = -offset.x; by = -offset.y;
    }

    const vec2_t &lp = pts[count - 1];
    vec2_t last(a00 * lp.x + a01 * lp.y + bx, a10 * lp.x + a11 * lp.y + by);

    for(size_t i = 0; i < count; i++) {
      const vec2_t &p = pts[i];
      vec2_t next(a00 * p.x + a01 * p.y + bx, a10 * p.x + a11 * p.y + by);

      add_line_segment_to_nodes(last, next, tb);
      last = next;
    }
  }

  int compare_nodes(const void* a, const void* b) {
    return *((int16_t*)a) - *((int16_t*)b);
  }

  uint8_t alpha_map_none[2] = {0, 255};
  uint8_t alpha_map_x4[5] = {0, 63, 127, 190, 255};
  uint8_t alpha_map_x16[17] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};

  rect_t render_nodes(rect_t *tb, uint aa, fill_rule_t fill_rule) {
    int minx = tb->w;
    int miny = tb->h;
    int maxx = 0;
    int maxy = 0;

    // supersample factor and sub-pixel mask for this antialias level
    int ss = 1 << aa;
    int mask = ss - 1;

    for(int y = 0; y < int(tb->h); y++) {
      int n = node_count_buffer[y];
      if(n == 0) {
        continue; // no nodes on this raster line
      }

      miny = min(miny, y);
      maxy = max(maxy, y);

      // sort scanline nodes (by packed value, i.e. by x)
      int16_t *nodes = &node_buffer[(y * MAX_NODES_PER_SCANLINE)];
      insertion_sort_i16(nodes, n);

      uint8_t *row_data = &tile_buffer[(y >> aa) * TILE_WIDTH];

      // accumulate one filled span [sx, ex) of coverage into the output row,
      // distributing partial sub-pixel coverage at the two ends.
      auto fill_span = [&](int sx, int ex) {
        if(sx >= ex) return;
        if(sx < minx) minx = sx;
        if(ex > maxx) maxx = ex;
        int o0 = sx >> aa;
        int o1 = (ex - 1) >> aa;
        if(o0 == o1) {
          row_data[o0] += (ex - sx);
        } else {
          row_data[o0] += ss - (sx & mask);
          for(int ox = o0 + 1; ox < o1; ox++) row_data[ox] += ss;
          row_data[o1] += ((ex - 1) & mask) + 1;
        }
      };

      if(fill_rule == NON_ZERO) {
        // fill where the running winding number is non-zero
        int winding = 0, span_start = 0;
        for(int i = 0; i < n; i++) {
          int nx = nodes[i] >> 1;
          int prev = winding;
          winding += (nodes[i] & 1) ? -1 : 1;
          if(prev == 0 && winding != 0) span_start = nx;
          else if(prev != 0 && winding == 0) fill_span(span_start, nx);
        }
      } else {
        // even-odd: each consecutive pair of crossings is a filled span
        for(int i = 0; i + 1 < n; i += 2) {
          fill_span(nodes[i] >> 1, nodes[i + 1] >> 1);
        }
      }
    }

    if(minx > maxx || miny > maxy) {
      return rect_t(0, 0, 0, 0);
    }

    int out_minx = (minx >> aa);
    int out_maxx = ((maxx + (1 << aa) - 1) >> aa);
    int out_miny = (miny >> aa);
    int out_maxy = ((maxy + (1 << aa) - 1) >> aa);

    return rect_t(out_minx, out_miny,
              (out_maxx - out_minx),
              (out_maxy - out_miny) + 1);
  }

  // Shared tile loop for shapes and glyphs. The only per-caller difference is
  // how nodes are built for each tile, passed in as `build_tile_nodes(tb, aa)`.
  template<typename BuildFn>
  void render_tiles(image_t *target, brush_t *brush, rect_t sb, BuildFn build_tile_nodes) {
    if(sb.empty()) return;

    // Clip the shape bounds to the target's clip region up front: bail entirely
    // if the shape is off-screen, and tighten the tile loop to the visible part
    // for partially-clipped shapes.
    rect_t clip = target->clip();
    sb = sb.intersection(clip);
    if(sb.empty()) return;

    // antialias level of target image
    uint aa = (uint)target->antialias();

    uint8_t *p_alpha_map = alpha_map_none;
    if(aa == 1) p_alpha_map = alpha_map_x4;
    if(aa == 2) p_alpha_map = alpha_map_x16;

    masked_span_func_t fn = target->_masked_span_func;
    span_func_t sfn = target->_span_func; // unmasked span for the aa == 0 fast path
    fill_rule_t fill_rule = target->fill_rule();

    // iterate over tiles
    for(int y = sb.y; y < sb.y + sb.h; y += TILE_HEIGHT) {
      for(int x = sb.x; x < sb.x + sb.w; x += TILE_WIDTH) {
        rect_t tb = clip.intersection(rect_t(x, y, TILE_WIDTH, TILE_HEIGHT)).intersection(sb).round();
        if(tb.empty()) { continue; } // if tile empty, skip it

        // screen coordinates for clipped tile (before antialias scaling)
        int sx = tb.x;
        int sy = tb.y;
        int sw = tb.w;
        int sh = tb.h;

        tb.x *= (1 << aa);
        tb.y *= (1 << aa);
        tb.w *= (1 << aa);
        tb.h *= (1 << aa);

        // clear node counts (needed by both the aa and aa == 0 paths)
        memset(node_count_buffer, 0, NODE_COUNT_BUFFER_SIZE);

        // the aa == 0 fast path emits spans directly and never reads the tile
        // coverage buffer, so only bother clearing it when antialiasing
        if(aa != 0) {
          for(int row = 0; row <= sh; ++row) {
            memset(&tile_buffer[row * TILE_WIDTH], 0, sw);
          }
        }

        // build the nodes for this tile (paths or glyph contours)
        PV_PROF_DECL(_t_build);
        build_tile_nodes(&tb, aa);
        PV_PROF_ACC(pv_us_build, _t_build);

        if(aa == 0) {
          // no antialiasing: every covered pixel is fully opaque, so emit spans
          // straight to the unmasked span function. this skips the coverage
          // buffer, the alpha-map pass, and the per-pixel mask multiplies in the
          // masked blend.
          PV_PROF_DECL(_t_raster);
          for(int y = 0; y < int(tb.h); y++) {
            int n = node_count_buffer[y];
            if(n == 0) { continue; }

            int16_t *nodes = &node_buffer[y * MAX_NODES_PER_SCANLINE];
            insertion_sort_i16(nodes, n);

            if(fill_rule == NON_ZERO) {
              int winding = 0, span_start = 0;
              for(int i = 0; i < n; i++) {
                int nx = nodes[i] >> 1;
                int prev = winding;
                winding += (nodes[i] & 1) ? -1 : 1;
                if(prev == 0 && winding != 0) span_start = nx;
                else if(prev != 0 && winding == 0 && span_start < nx) {
                  sfn(target, brush, sx + span_start, sy + y, nx - span_start);
                }
              }
            } else {
              for(int i = 0; i + 1 < n; i += 2) {
                int nsx = nodes[i] >> 1;
                int nex = nodes[i + 1] >> 1;
                if(nsx < nex) sfn(target, brush, sx + nsx, sy + y, nex - nsx);
              }
            }
          }
          PV_PROF_ACC(pv_us_raster, _t_raster);
          continue;
        }

        rect_t rb = render_nodes(&tb, aa, fill_rule).round();
        if(rb.empty()) { continue; }

        int rbx = rb.x;
        int rby = rb.y;
        int rbw = rb.w;
        int rbh = rb.h;

        for(int ty = rby; ty < rby + rbh; ty++) {
          // scale tile buffer coverage to alpha values
          uint8_t *p = &tile_buffer[ty * TILE_WIDTH + rbx];
          for(int c = rbw; c--; p++) *p = p_alpha_map[*p];

          // render tile span
          p = &tile_buffer[ty * TILE_WIDTH + rbx];
          fn(target, brush, sx + rbx, sy + ty, rbw, p);
        }
      }
    }
  }

  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush) {
    if(shape->paths.empty()) return;

    PV_PROF_DECL(_t_bounds);
    rect_t sb = shape->bounds().round();
    PV_PROF_ACC(pv_us_bounds, _t_bounds);

    render_tiles(target, brush, sb, [&](rect_t *tb, uint aa) {
      for(auto &path : shape->paths) build_nodes(&path, tb, transform, aa);
    });

#if PV_PROFILE
    // report cumulative phase times every 2880 shapes (~10 world.py frames)
    if(++pv_calls >= 2880) {
      char buf[128];
      int n = snprintf(buf, sizeof(buf),
                       "[pv] %d shapes: bounds=%luus build=%luus raster=%luus\n",
                       pv_calls, (unsigned long)pv_us_bounds, (unsigned long)pv_us_build,
                       (unsigned long)pv_us_raster);
      mp_hal_stdout_tx_strn_cooked(buf, n);
      pv_calls = 0;
      pv_us_bounds = pv_us_build = pv_us_raster = 0;
    }
#endif
  }






















  void build_glyph_nodes(glyph_path_t *path, rect_t *tb, mat3_t *transform, uint aa) {
    vec2_t offset = tb->tl();
    // start with the last point to close the loop, transform it, scale for antialiasing, and offset to tile origin
    glyph_path_point_t *p = &path->points[path->point_count - 1];
    vec2_t last = vec2_t(p->x, p->y);
    if(transform) last = last.transform(transform);
    last *= (1 << aa);
    last -= offset;

    for(int i = 0; i < path->point_count; i++) {
      p = &path->points[i];
      vec2_t next = vec2_t(p->x, p->y);
      if(transform) next = next.transform(transform);
      next *= (1 << aa);
      next -= offset;

      //printf("   - add line segment %d, %d -> %d, %d\n", int(last.x), int(last.y), int(next.x), int(next.y));
      add_line_segment_to_nodes(last, next, tb);
      last = next;
    }
  }

  void render_glyph(glyph_t *glyph, image_t *target, mat3_t *transform, brush_t *brush) {
    if(!glyph->path_count) return;

    render_tiles(target, brush, glyph->bounds(transform).round(), [&](rect_t *tb, uint aa) {
      for(int i = 0; i < glyph->path_count; i++) build_glyph_nodes(&glyph->paths[i], tb, transform, aa);
    });
  }
}
