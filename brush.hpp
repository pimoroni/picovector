#pragma once

#include <vector>
#include "picovector.hpp"
#include "image.hpp"

namespace picovector {

  struct brush {
    virtual ~brush() {
      debug_printf("brush destructed\n");      
    }
    
    void render_spans(image *target, _rspan *spans, int count);
    virtual void render_span(image *target, int x, int y, int w) = 0;
    virtual void render_mask(image *target);
    virtual void render_span_buffer(image *target, int x, int y, int w, uint8_t *sb) = 0;
  };
  
  struct color_brush : public brush {
    uint32_t color;    
    color_brush(int r, int g, int b, int a = 255);
    ~color_brush() {
      debug_printf("color brush destructed\n");      
    }
    
    void render_span(image *target, int x, int y, int w);
    void render_span_buffer(image *target, int x, int y, int w, uint8_t *sb);
    //void render_mask(image *target);
  };

  struct blur_brush : public brush {
    int passes;
    blur_brush(int passes = 1);
    
    void render_span(image *target, int x, int y, int w);
    void render_span_buffer(image *target, int x, int y, int w, uint8_t *sb) {};
    void render_mask(image *target);
  };

  struct brighten_brush : public brush {
    int amount;
    brighten_brush(int amount);
    
    void render_span(image *target, int x, int y, int w);
    void render_span_buffer(image *target, int x, int y, int w, uint8_t *sb) {};
    //void render_mask(image *target);
  };

  struct xor_brush : public brush {
    uint32_t color;    
    xor_brush(int r, int g, int b);
    
    void render_span(image *target, int x, int y, int w);
    void render_span_buffer(image *target, int x, int y, int w, uint8_t *sb);
    //void render_mask(image *target);
  };

}