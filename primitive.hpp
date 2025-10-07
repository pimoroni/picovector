#pragma once

#include "shape.hpp"

namespace picovector {

  shape* regular_polygon(float x, float y, float sides, float radius);
  shape* circle(float x, float y, float radius);
  shape* rectangle(float x, float y, float w, float h);
  shape* rounded_rectangle(float x, float y, float w, float h, float r1, float r2, float r3, float r4);
  shape* squircle(float x, float y, float size, float n=4.0f);
  shape* arc(float x, float y, float from, float to, float radius);
  shape* pie(float x, float y, float from, float to, float radius);
  shape* star(float x, float y, int spikes, float outer_radius, float inner_radius);
  shape* line(float x1, float y1, float x2, float y2, float w);
  
};
