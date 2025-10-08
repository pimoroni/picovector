#include <algorithm>

#include "font.hpp"
#include "image.hpp"
#include "picovector.hpp"
#include "brush.hpp"
#include "matrix.hpp"


using namespace std;

namespace picovector {
  struct _edgeinterp {
    point s;
    point e;
    float step;

    _edgeinterp() {

    }

    _edgeinterp(point p1, point p2) {
      if(p1.y < p2.y) { 
        s = p1; e = p2; 
      } else { 
        s = p2; e = p1; 
      }
      step = (e.x - s.x) / (e.y - s.y);
    }

    void next(float y, float *nodes, int &node_count) {
      if(y < s.y || y >= e.y) return;
      nodes[node_count++] = s.x + ((y - s.y) * step);
    }
  };

  void render_character(glyph *glyph, image *target, mat3 *transform, brush *brush) {    
    if(!glyph->path_count) {return;};
    
    // determine the intersection between transformed polygon and target image
    //rect b = glyph->bounds(transform);

    // clip the shape bounds to the target bounds
    //rect cb = b.intersection(target->bounds);
    rect cb = target->bounds;
    //debug_printf("print char %c at %f, %f -> %f, %f\n", char(glyph->codepoint), cb.x, cb.y, cb.w, cb.h);

    // setup interpolators for each edge of the polygon
    static _edgeinterp edge_interpolators[1024];
    int edge_interpolator_count = 0;

    for(int i = 0; i < glyph->path_count; i++) {
      glyph_path *path = &glyph->paths[i];

      point last = path->points[path->point_count - 1].transform(transform);
      //debug_printf("- adding path with %d points\n", int(path.points.size()));
      for(int j = 0; j < path->point_count; j++) {
        
        point next = path->points[j].transform(transform);

        // add new edge interpolator
        edge_interpolators[edge_interpolator_count] = _edgeinterp(last, next);
        edge_interpolator_count++;
        last = next;
      }
    }

    // for each scanline we step the interpolators and build the list of
    // intersecting nodes for that scaline
    static float nodes[128]; // up to 128 nodes (64 spans) per scanline
    const size_t SPAN_BUFFER_SIZE = 256;
    static _rspan spans[SPAN_BUFFER_SIZE];

    static uint8_t sb[SPAN_BUFFER_SIZE];    

    int aa = target->antialias;    
    
    int sy = cb.y;
    int ey = cb.y + cb.h;

    // TODO: we can special case a faster version for no AA here

    int span_count = 0;
    for(float y = sy; y < ey; y++) {
      //debug_printf("y = %f\n", y);
      // clear the span buffer
      memset(sb, 0, sizeof(sb));

      // loop over y sub samples
      for(int yss = 0; yss < aa; yss++) {
        float ysso = (1.0f / float(aa + 1)) * float(yss + 1);
      
        int node_count = 0;

        for(int i = 0; i < edge_interpolator_count; i++) {
          //debug_printf("ei = %f -> %f\n", edge_interpolators[i].s.y, edge_interpolators[i].e.y);
          edge_interpolators[i].next(y + ysso, nodes, node_count);
        }

        // sort the nodes so that neighouring pairs represent render spans
        sort(nodes, nodes + node_count);
               
        for(int i = 0; i < node_count; i += 2) {
          int x1 = round((nodes[i + 0] - cb.x) * aa);
          int x2 = round((nodes[i + 1] - cb.x) * aa);

          x1 = min(max(0, x1), int(cb.w * aa));
          x2 = min(max(0, x2), int(cb.w * aa));   
          
          uint8_t *psb = sb;
          for(int j = x1; j < x2; j++) {
            psb[j >> (aa >> 1)]++;
          }        
        }
      }

      // todo: this could be more efficient if we buffer multiple scanlines at once
      //debug_printf("render_span_buffer\n");
      static uint8_t aa_none[2] = {0, 255};
      static uint8_t aa_x2[5] = {0, 64, 128, 192, 255};
      static uint8_t aa_x4[17] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255};

      uint8_t *aa_lut = aa_none;
      aa_lut = aa == 2 ? aa_x2 : aa_lut;
      aa_lut = aa == 4 ? aa_x4 : aa_lut;      

      // scale span buffer alpha values
      int c = SPAN_BUFFER_SIZE;
      uint8_t *psb = sb;
      while(c--) {
        *psb = aa_lut[*psb];
        psb++;
      }

      brush->render_span_buffer(target, cb.x, y, cb.w, sb);
    }
  }

  point glyph_path_point::transform(mat3 *transform) {
    return point(
      transform->v00 * float(x) + transform->v01 * float(y) + transform->v02,
      transform->v10 * float(x) + transform->v11 * float(y) + transform->v12
    );
  }

  rect glyph::bounds(mat3 *transform) {
    point p1(x, y);
    point p2(x + w, y);
    point p3(x + w, y + h);
    point p4(x, y + h);

    p1 = p1.transform(transform);
    p2 = p2.transform(transform);
    p3 = p3.transform(transform);
    p4 = p4.transform(transform);

    float minx = min(p1.x, min(p2.x, min(p3.x, p4.x)));
    float miny = min(p1.y, min(p2.y, min(p3.y, p4.y)));
    float maxx = max(p1.x, max(p2.x, max(p3.x, p4.x)));
    float maxy = max(p1.y, max(p2.y, max(p3.y, p4.y)));

    return rect(minx, miny, ceil(maxx) - minx, ceil(maxy) - miny);
  }

  rect font::measure(image *target, const char *text, float size) {
    rect r = {0, 0, 0, 0};

    mat3 transform;
    transform = transform.scale(size / 128.0f, size / 128.0f);    
    
    for(size_t i = 0; i < strlen(text); i++) {
      char c = text[i];
      // find the glyph
      for(int j = 0; j < this->glyph_count; j++) {
        if(this->glyphs[j].codepoint == uint16_t(c)) {
          float a = this->glyphs[j].advance;
          transform = transform.translate(a, 0);
          point caret(1, 1);
          caret = caret.transform(transform);
          r.w = max(r.w, caret.x);
          r.h = max(r.y, caret.y);
        }
      }
    }

    return r;
  }

  void font::draw(image *target, const char *text, float x, float y, float size) {    
    point caret(x, y);

    mat3 transform;
    transform = transform.translate(x, y);
    transform = transform.scale(size / 128.0f, size / 128.0f);    
    
    for(size_t i = 0; i < strlen(text); i++) {
      char c = text[i];
      // find the glyph
      for(int j = 0; j < this->glyph_count; j++) {
        if(this->glyphs[j].codepoint == uint16_t(c)) {
          render_character(&this->glyphs[j], target, &transform, target->brush);
          float a = this->glyphs[j].advance;
          transform = transform.translate(a, 0);
        }
      }
    }
  }

}