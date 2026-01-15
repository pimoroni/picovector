#include <algorithm>

#include "picovector.hpp"
#include "brush.hpp"
#include "image.hpp"
#include "shape.hpp"
#include "types.hpp"
#include "mat3.hpp"
#include "blend.hpp"

using std::sort, std::min, std::max;

// memory pool for rasterisation, png decoding, and other memory intensive
// tasks (sized to fit PNGDEC state) - on pico it *must* be 32bit aligned (i
// found out the hard way.)
char __attribute__((aligned(4))) PicoVector_working_buffer[working_buffer_size];

#define TILE_WIDTH 128
#define TILE_HEIGHT 32
#define MAX_NODES_PER_SCANLINE 64

#define TILE_BUFFER_SIZE (TILE_WIDTH * TILE_HEIGHT * sizeof(uint8_t)) // 4kB tile buffer
#define NODE_BUFFER_ROW_SIZE (MAX_NODES_PER_SCANLINE * sizeof(int16_t)) // 16kB node buffer
#define NODE_BUFFER_SIZE (TILE_HEIGHT * 4 * NODE_BUFFER_ROW_SIZE) // 32kB node buffer
#define NODE_COUNT_BUFFER_SIZE (TILE_HEIGHT * 4 * sizeof(uint8_t)) // 256 byte node count buffer

// buffer that each tile is rendered into before callback
uint8_t *tile_buffer = (uint8_t *)&PicoVector_working_buffer[0];
int16_t *node_buffer = (int16_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE];
uint8_t *node_count_buffer = (uint8_t *)&PicoVector_working_buffer[TILE_BUFFER_SIZE + NODE_BUFFER_SIZE];


namespace picovector {


  // struct _edgeinterp {
  //   vec2_t s;
  //   vec2_t e;
  //   float step;

  //   _edgeinterp() {
  //   }

  //   _edgeinterp(vec2_t p1, vec2_t p2) {
  //     if(p1.y < p2.y) {
  //       s = p1; e = p2;
  //     } else {
  //       s = p2; e = p1;
  //     }
  //     step = (e.x - s.x) / (e.y - s.y);
  //   }

  //   void next(float y, float *nodes, int &node_count) {
  //     if(y < s.y || y >= e.y) return;
  //     nodes[node_count++] = s.x + ((y - s.y) * step);
  //   }
  // };


  // void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush) {
  //   if(!shape->paths.size()) {return;};

  //   // determine the intersection between transformed polygon and target image
  //   rect_t b = shape->bounds();

  //   // clip the shape bounds to the target bounds
  //   rect_t cb = b.intersection(target->clip());
  //   cb.x = floor(cb.x);
  //   cb.y = floor(cb.y);
  //   cb.w = ceil(cb.w);
  //   cb.h = ceil(cb.h);

  //   //debug_printf("rendering shape %p with %d paths\n", (void*)shape, int(shape->paths.size()));
  //   //debug_printf("setup interpolators\n");
  //   // setup interpolators for each edge of the polygon
  //   //static _edgeinterp edge_interpolators[256];
  //   auto edge_interpolators = new(PicoVector_working_buffer) _edgeinterp[256];
  //   int edge_interpolator_count = 0;
  //   for(path_t &path : shape->paths) {
  //     vec2_t last = path.points.back(); // start with last vec2 to close loop
  //     last = last.transform(transform);
  //     //debug_printf("- adding path with %d vec2s\n", int(path.vec2s.size()));
  //     for(vec2_t next : path.points) {
  //       next = next.transform(transform);
  //       // add new edge interpolator
  //       edge_interpolators[edge_interpolator_count] = _edgeinterp(last, next);
  //       edge_interpolator_count++;
  //       last = next;
  //     }
  //   }

  //   // for each scanline we step the interpolators and build the list of
  //   // intersecting nodes for that scaline
  //   static float nodes[128]; // up to 128 nodes (64 spans) per scanline
  //   const size_t SPAN_BUFFER_SIZE = 512;
  //   //static _rspan spans[SPAN_BUFFER_SIZE];
  //   static auto spans = new((uint8_t *)PicoVector_working_buffer + (sizeof(_edgeinterp) * 256)) _rspan[SPAN_BUFFER_SIZE];

  //   //static uint8_t sb[SPAN_BUFFER_SIZE];
  //   static auto sb = new((uint8_t *)PicoVector_working_buffer + (sizeof(_edgeinterp) * 256) + (sizeof(_edgeinterp) * SPAN_BUFFER_SIZE)) uint8_t[SPAN_BUFFER_SIZE];

  //   int aa = target->antialias();

  //   int sy = cb.y;
  //   int ey = cb.y + cb.h;

  //   // TODO: we can special case a faster version for no AA here

  //   int span_count = 0;
  //   for(float y = sy; y < ey; y++) {
  //     // clear the span buffer
  //     memset(sb, 0, cb.w);

  //     // loop over y sub samples
  //     for(int yss = 0; yss < aa; yss++) {
  //       float ysso = (1.0f / float(aa + 1)) * float(yss + 1);

  //       int node_count = 0;

  //       for(int i = 0; i < edge_interpolator_count; i++) {
  //         edge_interpolators[i].next(y + ysso, nodes, node_count);
  //       }

  //       // sort the nodes so that neighouring pairs represent render spans
  //       sort(nodes, nodes + node_count);

  //       for(int i = 0; i < node_count; i += 2) {
  //         int x1 = round((nodes[i + 0] - cb.x) * aa);
  //         int x2 = round((nodes[i + 1] - cb.x) * aa);

  //         x1 = min(max(0, x1), int(cb.w * aa));
  //         x2 = min(max(0, x2), int(cb.w * aa));
  //         uint8_t *psb = sb;
  //         for(int j = x1; j < x2; j++) {
  //           psb[j >> (aa >> 1)]++;
  //         }
  //       }
  //     }

  //     // todo: this could be more efficient if we buffer multiple scanlines at once
  //     //debug_printf("render_span_buffer\n");
  //     static uint8_t aa_none[2] = {0, 255};
  //     static uint8_t aa_x2[5] = {0, 64, 128, 192, 255};
  //     static uint8_t aa_x4[17] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};

  //     uint8_t *aa_lut = aa_none;
  //     aa_lut = aa == 2 ? aa_x2 : aa_lut;
  //     aa_lut = aa == 4 ? aa_x4 : aa_lut;

  //     // scale span buffer alpha values
  //     int c = cb.w;
  //     uint8_t *psb = sb;
  //     while(c--) {
  //       *psb = aa_lut[*psb];
  //       psb++;
  //     }

  //     brush->mask_span_func(brush, cb.x, y, cb.w, sb);
  //     //brush->render_span_buffer(target, cb.x, y, cb.w, sb);
  //   }

  //   bool _debug_vec2s = false;
  //   if(_debug_vec2s) {
  //     color_brush_t white(target, rgba(255, 255, 255, 50));
  //     for(path_t &path : shape->paths) {
  //       vec2_t last = path.points.back(); // start with last vec2 to close loop
  //       last = last.transform(transform);
  //       //debug_printf("- adding path with %d vec2s\n", int(path.vec2s.size()));
  //       for(vec2_t next : path.points) {
  //         // _rspan span = {.x = next.x .y = next.y, w = 1, o = 255};
  //         if(next.x >= 0 && next.x < 160 && next.y >= 0 && next.y < 120) {
  //           white.pixel_func(&white, next.x, next.y);
  //           //white.render_span(target, next.x, next.y, 1);
  //         }
  //       }
  //     }
  //   }
  // }



int sign(int v) {return (v > 0) - (v < 0);}

void add_line_segment_to_nodes(const vec2_t start, const vec2_t end, rect_t *tb) {
  int sx = start.x, sy = start.y, ex = end.x, ey = end.y;

  if(ey < sy) {
    // swap endpoints if line "pointing up", we do this because we
    // alway skip the last scanline (so that polygons can butt cleanly
    // up against each other without overlap)
    int ty = sy; sy = ey; ey = ty;
    int tx = sx; sx = ex; ex = tx;
  }

  // early out if line is completely outside the tile, or has no gradient
  if (ey <= 0 || sy >= int(tb->h) || sy == ey) return;

  // determine how many in-bounds lines to render
  int y = max(0, sy);
  int count = min(int(tb->h), ey) - y;

  int minx = 0;//floor(tb->x);
  int maxx = ceil(tb->w);//ceil(tb->x + tb->w);
  int x = sx;
  int e = 0;

  const int xinc = sign(ex - sx);
  const int einc = abs(ex - sx) + 1;
  const int dy = ey - sy;

  // if sy < 0 jump to the start
  if (sy < 0) {
    e = einc * -sy;
    int xjump = e / dy;
    e -= dy * xjump;
    x += xinc * xjump;
  }

  // loop over scanlines
  while(count--) {
    // consume accumulated error
    while(e > dy) {e -= dy; x += xinc;}

    // clamp node x value to tile bounds
    int nx = max(min(x, maxx), minx);
    //printf("      + adding node at %d, %d\n", nx, y);
    // add node to node list
    node_buffer[(y * MAX_NODES_PER_SCANLINE) + node_count_buffer[y]] = nx;
    node_count_buffer[y]++;

    // step to next scanline and accumulate error
    y++;
    e += einc;
  }
}

void build_nodes(path_t *path, rect_t *tb, mat3_t *transform, uint aa) {
  vec2_t offset = tb->tl();
  // start with the last point to close the loop, transform it, scale for antialiasing, and offset to tile origin
  vec2_t last = path->points[path->points.size() - 1];
  if(transform) last = last.transform(transform);
  last *= (1 << aa);
  last -= offset;

  for(auto next : path->points) {
    if(transform) next = next.transform(transform);
    next *= (1 << aa);
    next -= offset;

    //printf("   - add line segment %d, %d -> %d, %d\n", int(last.x), int(last.y), int(next.x), int(next.y));
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

rect_t render_nodes(rect_t *tb, uint aa) {
  int minx = ceil(tb->w);
  int miny = ceil(tb->h);
  int maxx = 0;
  int maxy = 0;

  for(int y = 0; y < int(tb->h); y++) {
    if(node_count_buffer[y] == 0) continue; // no nodes on this raster line

    miny = min(miny, y);
    maxy = max(maxy, y);

    // sort scanline nodes
    int16_t *nodes = &node_buffer[(y * MAX_NODES_PER_SCANLINE)];
    std::sort(nodes, nodes + node_count_buffer[y]);

    unsigned char* row_data = &tile_buffer[(y >> aa) * TILE_WIDTH];

    for(uint32_t i = 0; i < node_count_buffer[y]; i += 2) {
      int sx = *nodes++;
      int ex = *nodes++;

      if(sx == ex) { // empty span, nothing to do
        continue;
      }

      minx = min(minx, sx);
      maxx = max(maxx, ex);

      // rasterise the span into the tile buffer
      do {
        row_data[sx >> aa]++;
      } while(++sx < ex);
    }
  }

  // rb.w = maxx - minx;

  // shifting the width and height effectively "floors" the result which can
  // mean we lose a pixel off the right or bottom edge of the tile. by adding
  // either 1 (at x4) or 3 (at x16) we change that to a "ceil" instead ensuring
  // the full tile bounds are returned
  // if(aa) {
  //   int maxx = rb.x + rb.w + (aa | 0b1);
  //   int maxy = rb.y + rb.h + (aa | 0b1);

  //   rb.x = int(rb.x) >> aa;
  //   rb.y = int(rb.y) >> aa;
  //   rb.w = (maxx >> aa) - int(rb.x);
  //   rb.h = (maxy >> aa) - int(rb.y);
  // }

  if(minx > maxx || miny > maxy) {
    return rect_t(0, 0, 0, 0);
  }

  return rect_t(minx >> aa, miny >> aa, (maxx - minx) >> aa, (maxy - miny) >> aa);
}




  void render(shape_t *shape, image_t *target, mat3_t *transform, brush_t *brush) {

    if(shape->paths.empty()) return;

    // antialias level of target image
    uint aa = (uint)target->antialias();

    uint8_t *p_alpha_map = alpha_map_none;
    if(aa == 1) p_alpha_map = alpha_map_x4;
    if(aa == 2) p_alpha_map = alpha_map_x16;

    mask_span_func_t sf = brush->mask_span_func;
    //printf("render shape\n");

    //printf("aa = %d\n", aa);
    // determine bounds of shape to be rendered
    rect_t sb = shape->bounds();

    // clamp bounds to integer values
    int sbx = int(floor(sb.x));
    int sby = int(floor(sb.y));
    int sbw = int( ceil(sb.w));
    int sbh = int( ceil(sb.h));

    rect_t clip = target->clip();
    //printf("- shape bounds %d, %d (%d x %d)\n", sbx, sby, sbw, sbh);
    //printf("- clip bounds %d, %d (%d x %d)\n", int(clip.x), int(clip.y), int(clip.w), int(clip.h));

    // iterate over tiles
    //printf("> processing tiles\n");
    for(int y = sby; y < sby + sbh; y += TILE_HEIGHT) {
      for(int x = sbx; x < sbx + sbw; x += TILE_WIDTH) {
        //printf(" > tile %d x %d\n", x, y);
        rect_t tb = rect_t(x, y, TILE_WIDTH, TILE_HEIGHT);

        //printf("  - tile bounds %d, %d (%d x %d)\n", int(tb.x), int(tb.y), int(tb.w), int(tb.h));

        tb = clip.intersection(tb);
        if(tb.empty()) { continue; } // if tile empty, skip it

        //printf("  - clipped tile bounds %d, %d (%d x %d)\n", int(tb.x), int(tb.y), int(tb.w), int(tb.h));
        // screen coordinates for clipped tile
        int sx = int(floor(tb.x));
        int sy = int(floor(tb.y));
        int sw = int(ceil(tb.w));
        int sh = int(ceil(tb.h));

        tb.x = int(floor(tb.x)) * (1 << aa);
        tb.y = int(floor(tb.y)) * (1 << aa);
        tb.w = int( ceil(tb.w)) * (1 << aa);
        tb.h = int( ceil(tb.h)) * (1 << aa);

//        offset *= (1 << aa);
        //printf("  - clipped and scaled tile bounds %d, %d (%d x %d)\n", int(tb.x), int(tb.y), int(tb.w), int(tb.h));

        // clear existing tile data and nodes
        memset(node_count_buffer, 0, NODE_COUNT_BUFFER_SIZE);
        memset(tile_buffer, 0, TILE_BUFFER_SIZE);

        //printf("  - build nodes\n");
        // build the nodes for each pp_path_t
        for(auto &path : shape->paths) {
          // debug("    : build nodes for path (%d points)\n", path->count);
          build_nodes(&path, &tb, transform, aa);
        }


        // continue;

        // debug("    : render the tile\n");
        //printf("  - render nodes\n");

        rect_t rb = render_nodes(&tb, aa);
        // rect_t rb = render_nodes(&tb, aa);
        // tb.x += rb.x; tb.y += rb.y; tb.w = rb.w; tb.h = rb.h;

        if(tb.empty()) { continue; }

        // pp_tile_t tile = {
        //   .x = tb.x, .y = tb.y, .w = tb.w, .h = tb.h,
        //   .stride = PP_TILE_BUFFER_SIZE,
        //   .data = tile_buffer + rb.x + (PP_TILE_BUFFER_SIZE * rb.y)
        // };

        //brush->
        // _pp_tile_callback(&tile);
        //printf("  - render tile\n");



  // for(int y = tb.y; y < tb.y + tb.h; y++) {
  //   unsigned char* row_data = &tile_buffer[(y * TILE_SIZE) + int(rb.x)];
  //   for(int x = rb.x; x < rb.x + rb.w; x++) {
  //     *row_data = p_alpha_map[*row_data];
  //     row_data++;
  //   }
  // }

        // int c = TILE_WIDTH * TILE_HEIGHT;
        // uint8_t* p = tile_buffer;
        // while(c--) {
        //   *p = p_alpha_map[*p];
        //   p++;
        // }
        //printf("! render tile at %d, %d (%d x %d)\n", sx, sy, sw, sh);

        //p = tile_buffer;

        //printf("%d vs %d\n", int(rb.h), sh);

        int rbx = int(floor(rb.x));
        int rby = int(floor(rb.y));
        int rbw = int(ceil(rb.w));
        int rbh = int(ceil(rb.h));

        for(int ty = rby; ty <= rby + rbh; ty++) {
          uint8_t* p = &tile_buffer[ty * TILE_WIDTH + rbx];
          int c = rbw;
          while(c--) {
            *p = p_alpha_map[*p];
            p++;
          }
          // brush_t *brush, int x, int y, int w, uint8_t *mask
          p = &tile_buffer[ty * TILE_WIDTH + rbx];
          sf(brush, sx + rbx, sy + ty, rbw, p);
         // p += TILE_WIDTH;
        }
      }
    }
  }
}
