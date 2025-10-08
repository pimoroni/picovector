#pragma once

#include <stdint.h>
#ifdef PICO
#include "hardware/interp.h"
#else
#define __not_in_flash_func(v) v
#endif
#include <cstring>

inline uint32_t __not_in_flash_func(_make_col)(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  return __builtin_bswap32((r << 24) | (g << 16) | (b << 8) | a);
}

inline uint8_t __not_in_flash_func(_r)(uint32_t c) { return ((uint8_t*)&c)[0]; }
inline uint8_t __not_in_flash_func(_g)(uint32_t c) { return ((uint8_t*)&c)[1]; }
inline uint8_t __not_in_flash_func(_b)(uint32_t c) { return ((uint8_t*)&c)[2]; }
inline uint8_t __not_in_flash_func(_a)(uint32_t c) { return ((uint8_t*)&c)[3]; }


inline void __not_in_flash_func(_rgba_blend_to)(uint32_t *dst, uint32_t *src) {
  uint8_t *pd = (uint8_t *)dst;
  uint8_t *ps = (uint8_t *)src;
  uint8_t a = ps[3];
  pd[0] = ((pd[0] * (255 - a)) + (ps[0] * a)) / 255;
  pd[1] = ((pd[1] * (255 - a)) + (ps[1] * a)) / 255;
  pd[2] = ((pd[2] * (255 - a)) + (ps[2] * a)) / 255;
  pd[3] = 255; // TODO: this is wrong
}

inline void __not_in_flash_func(_rgba_blend_to)(uint32_t *dst, uint32_t *src, uint8_t a) {
  uint8_t *pd = (uint8_t *)dst;
  uint8_t *ps = (uint8_t *)src;
  a = (a * ps[3]) / 255;
  pd[0] = ((pd[0] * (255 - a)) + (ps[0] * a)) / 255;
  pd[1] = ((pd[1] * (255 - a)) + (ps[1] * a)) / 255;
  pd[2] = ((pd[2] * (255 - a)) + (ps[2] * a)) / 255;
  pd[3] = 255; // TODO: this is wrong
}

inline void __not_in_flash_func(span_argb8)(uint32_t *dst, int32_t w, uint32_t c) { 
  
  uint8_t *ps = (uint8_t *)&c;

//   if(ps[3] == 0) {
//     // zero alpha, skip span
//   } else if (ps[3] == 255) {
//     // full alpha copy pixel
//     while(w--) {
//       *dst++ = c;
//     }
//   } else {
// #ifdef PICO
//     interp0->accum[1] = ps[3]; // alpha    
//     while(w--) {
//       uint8_t *pd = (uint8_t *)dst;  
    
//       interp0->base[0] = pd[0];
//       interp0->base[1] = ps[0]; // red
//       pd[0] = (uint8_t)interp0->peek[1];
    
//       interp0->base[0] = pd[1];
//       interp0->base[1] = ps[1]; // green
//       pd[1] = (uint8_t)interp0->peek[1];
    
//       interp0->base[0] = pd[2];
//       interp0->base[1] = ps[2]; // blue
//       pd[2] = (uint8_t)interp0->peek[1];

//       dst++;
//     }
// #else


    while(w--) {
      _rgba_blend_to(dst++, &c);
      
      // uint8_t *pd = (uint8_t *)dst;
      // uint8_t a = (pd[3] * ps[3]) >> 8;
      // pd[0] = ((pd[0] * (255 - a)) + (ps[0] * a)) / 255;
      // pd[1] = ((pd[1] * (255 - a)) + (ps[1] * a)) / 255;
      // pd[2] = ((pd[2] * (255 - a)) + (ps[2] * a)) / 255;
      // dst++;
    }
// #endif
//   }
}


inline void __not_in_flash_func(span_argb8)(uint32_t *dst, int32_t w, uint32_t c, uint8_t *m) { 
  uint8_t *ps = (uint8_t *)&c;
  while(w--) {
    _rgba_blend_to(dst++, &c, *m++);    
  }
}

inline void __not_in_flash_func(span_blit_argb8)(uint32_t *src, uint32_t *dst, int w, int a = 255) {     
  //span_pixels_drawn += w;

  //src = _buffer_span(src, w); // buffer span from psram to sram
  while(w--) {
    uint8_t *ps = (uint8_t *)src;  
    uint8_t *pd = (uint8_t *)dst;  

    int ca = (ps[3] * (a)) / 255; // apply global alpha

    if(ca == 0) {
      // zero alpha, skip pixel                            
    } else if (ca == 255) {
      // full alpha copy pixel
      *dst = *src;
    } else {
#ifdef PICO
      // alpha requires blending pixel
      interp0->accum[1] = ca;
    
      interp0->base[0] = pd[0];
      interp0->base[1] = ps[0]; // red
      pd[1] = (uint8_t)interp0->peek[0];
    
      interp0->base[0] = pd[1];
      interp0->base[1] = ps[1]; // green
      pd[1] = (uint8_t)interp0->peek[1];
    
      interp0->base[0] = pd[2];
      interp0->base[1] = ps[2]; // blue
      pd[2] = (uint8_t)interp0->peek[1];
#else
      pd[0] = ((pd[0] * (255 - ca)) + (ps[0] * ca)) / 255;
      pd[1] = ((pd[1] * (255 - ca)) + (ps[1] * ca)) / 255;
      pd[2] = ((pd[2] * (255 - ca)) + (ps[2] * ca)) / 255;
#endif
    }

    src++;
    dst++;
  }  
}

inline void __not_in_flash_func(span_blit_scale)(uint32_t *src, uint32_t *dst, int srcx, int srcstepx, int w, int a) {  
  while(w--) {    
    uint8_t *pd = (uint8_t *)dst;
    uint8_t *ps = (uint8_t *)(src + (srcx >> 16));

    int ca = (ps[3] * (a + 1)) / 256; // apply global alpha

    if(ca == 0) {
      // zero alpha, skip pixel                            
    } else if (ca == 255) {
      // full alpha copy pixel
      *dst = src[srcx >> 16];
    } else {
#ifdef PICO
      // alpha requires blending pixel
      interp0->accum[1] = ca;
    
      interp0->base[0] = pd[1];
      interp0->base[1] = ps[1]; // red
      pd[1] = (uint8_t)interp0->peek[1];
    
      interp0->base[0] = pd[2];
      interp0->base[1] = ps[2]; // green
      pd[2] = (uint8_t)interp0->peek[1];
    
      interp0->base[0] = pd[3];
      interp0->base[1] = ps[3]; // blue
      pd[3] = (uint8_t)interp0->peek[1];
#else
      pd[0] = ((pd[0] * (255 - ca)) + (ps[0] * ca)) / 255;
      pd[1] = ((pd[1] * (255 - ca)) + (ps[1] * ca)) / 255;
      pd[2] = ((pd[2] * (255 - ca)) + (ps[2] * ca)) / 255;
#endif
    }
    
    srcx += srcstepx;
    dst++;
  }
}
