#include <algorithm>

#include "picovector.hpp"
#include "brush.hpp"
#include "matrix.hpp"

using namespace std;

// This will completely break imgui or sokol or something
//because these will be called before the MicroPython heap is initialised.

bool micropython_gc_enabled = false;
/*
void * operator new(std::size_t n)// throw(std::bad_alloc)
{
    //return malloc(n);
    if(micropython_gc_enabled) {
      std::cout << "new: m_tracked_calloc(" << n << ")" << std::endl;
      return m_tracked_calloc(n, 1);
    } else {
      return malloc(n);
    }
}

void operator delete(void * p)// throw()
{
    //std::cout << "free: " << reinterpret_cast<void*>(p) << std::dec << std::endl;
    //free(p);
    if(micropython_gc_enabled) {
      std::cout << "free: m_tracked_free(" << reinterpret_cast<void*>(p) << std::dec << ")" << std::endl;
      m_tracked_free(p);
    } else {
      free(p);
    }
}
*/

using namespace std;

//#define debug_printf(fmt, ...)
//#define debug_printf(fmt, ...) fprintf(stdout, fmt, ##__VA_ARGS__)

namespace picovector {
  struct _edgeinterp {
    point s;
    point e;
    float step;

    _edgeinterp() {

    }

    _edgeinterp(point p1, point p2) {
      if(p1.y < p2.y) { s = p1; e = p2; }else{ s = p2; e = p1; }
      step = (e.x - s.x) / (e.y - s.y);
    }

    void next(int y, float *nodes, int &node_count) {
      if(y < s.y || y >= e.y) return;
      nodes[node_count++] = s.x + ((y - s.y) * step);
    }
  };

  void render(shape *shape, image *target, mat3 *transform, brush *brush) {    
    if(!shape->paths.size()) {return;};
    
    // determine the intersection between transformed polygon and target image
    rect b = shape->bounds();

    //debug_printf("rendering shape %p with %d paths\n", (void*)shape, int(shape->paths.size()));
    //debug_printf("setup interpolators\n");
    // setup interpolators for each edge of the polygon
    static _edgeinterp edge_interpolators[256];
    int edge_interpolator_count = 0;
    for(path &path : shape->paths) {
      point last = path.points.back(); // start with last point to close loop
      last = last.transform(transform);
      //debug_printf("- adding path with %d points\n", int(path.points.size()));
      for(point next : path.points) {
        next = next.transform(transform);
        // add new edge interpolator
        edge_interpolators[edge_interpolator_count] = _edgeinterp(last, next);
        edge_interpolator_count++;
        last = next;
      }
    }


    // clear target mask
    for(int y = 0; y < target->bounds.h; y++) {
      uint8_t *pdst = (uint8_t*)target->ptr(0, y);
      for(int x = 0; x < target->bounds.w; x++) {
        pdst[3] = 0;
        pdst += 4;
      }
    }
    
    //debug_printf("get nodes\n");
    // for each scanline we step the interpolators and build the list of
    // intersecting nodes for that scaline
    static float nodes[128]; // up to 128 nodes (64 spans) per scanline
    // const size_t SPAN_BUFFER_SIZE = 256;
    // static _rspan spans[SPAN_BUFFER_SIZE];

    int sy = max(b.y, 0.0f);
    int ey = min(floor(b.y) + ceil(b.h), target->bounds.h);
    for(int y = sy; y <= ey; y++) {
      int node_count = 0;
      for(int i = 0; i < edge_interpolator_count; i++) {
        edge_interpolators[i].next(y, nodes, node_count);
      }

      // sort the nodes so that neighouring pairs represent render spans
      sort(nodes, nodes + node_count);


      // render into target mask channel
      float *current_node = nodes;
      while(node_count > 0) {
        int x1 = min(max(0.0f, current_node[0]), target->bounds.w - 1);
        int x2 = min(max(0.0f, current_node[1]), target->bounds.w - 1);

        current_node += 2;
        node_count -= 2;

        uint8_t *pdst = (uint8_t*)target->ptr(x1, y);

        for(int i = 0; i < x2 - x1; i++) {
          pdst[3] = 255;
          pdst += 4;
        }
      }

      

      // float *current_node = nodes;
      // int span_idx = 0;
      // while(node_count > 0) {
      //   int x1 = min(max(0.0f, current_node[0]), target->bounds.w);
      //   int x2 = min(max(0.0f, current_node[1]), target->bounds.w);

      //   spans[span_idx].x = x1;
      //   spans[span_idx].y = y;
      //   spans[span_idx].w = x2 - x1;
      //   spans[span_idx].o = 255;
      //   span_idx++;
      //   current_node += 2;
      //   node_count -= 2;

      //   if(span_idx == SPAN_BUFFER_SIZE || node_count == 0) {
      //     //debug_printf("render spans %d\n", span_idx);
      //     brush->render_spans(target, spans, span_idx);
      //     span_idx = 0;
      //   }
      // }
    }

    brush->render_mask(target);

    for(int y = 0; y < target->bounds.h; y++) {
      uint8_t *pdst = (uint8_t*)target->ptr(0, y);
      for(int x = 0; x < target->bounds.w; x++) {
        pdst[3] = 255;
        pdst += 4;
      }
    }

    //debug_printf("render done\n");
  }

  
  /* ==========================================================================

  PRIMITIVES

  helper functions for making primitive shapes

  ========================================================================== */



}