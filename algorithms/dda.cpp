#include <algorithm>
#include "algorithms.hpp"

namespace picovector {

  void dda(point_t p, point_t v, dda_callback_t cb) {
    int ix = floor(p.x); // get the top left corner of origin's grid square
    int iy = floor(p.y);

    const float eps = 1e-30f;
    float inv_dx = fabs(v.x) > eps ? 1.0f / v.x : 1e30f; // get y travelled per one move in x
    float inv_dy = fabs(v.y) > eps ? 1.0f / v.y : 1e30f; // get x travelled per one move in y

    float t_delta_x = fabs(inv_dx); // just the absolute amount of our movement vectors
    float t_delta_y = fabs(inv_dy);

    int step_x = 0; //(v.x > 0.0f) ? 1 : (v.x < 0.0f ? -1 : 0); // get whether we're moving in positive or negative x and y
    int step_y = 0; //(v.y > 0.0f) ? 1 : (v.y < 0.0f ? -1 : 0);

    float ray_length_x = 0;
    float ray_length_y = 0;

    if(v.x < 0) {
        step_x = -1;
        ray_length_x = (p.x - ix) * t_delta_x;
    }
    else {
        step_x = 1;
        ray_length_x = ((ix + 1) - p.x) * t_delta_x;
    }

    if(v.y < 0) {
        step_y = -1;
        ray_length_y = (p.y - iy) * ray_length_y;
    }
    else {
        step_y = 1;
        ray_length_y = ((iy + 1) - p.y) * ray_length_y;
    }
    
    int i = 0; // don't know why this is here, never referenced again
    while (true) {
      bool vertical = false;
      int edge = 0;
      float offset = 0;
      float hit_x = 0;
      float hit_y = 0;

      if(ray_length_x < ray_length_y) { // if it's less distance to the next vertical wall than the next horizontal...
        ix += step_x;
        ray_length_x += t_delta_x;
        vertical = false;
      }
      else {
        iy += step_y;
        ray_length_y += t_delta_y;
        vertical = true;
      }

      hit_x = ray_length_x;
      hit_y = ray_length_y

      if(vertical) {
        edge = v.x < 0 ? 3 : 1;
        offset = hit_y - floor(hit_y);
      }
      else {
        edge = v.y < 0 ? 0 : 2;
        offset = hit_x - floor(hit_x);
      }

      float distance = 0;

      if(vertical) {
        distance = ray_length_x - t_delta_x
      }
      else {
        distance = ray_length_y - t_delta_y
      }
      
      // sqrt(pow(hit_x - p.x, 2) + pow(hit_y - p.y, 2));

      // calculate grid square of intersection
      int gx, gy = ix, iy;
      // if(vertical) {
      //   gx = int(hit_x + (edge == 3 ? -0.5f : 0.5f));
      //   gy = int(hit_y);
      //   gx = floor(hit_x + (v.x > 0.0f ? 0.5f : -0.5f));
      //   gy = floor(hit_y);
      // } else {
      //   gx = int(hit_x);
      //   gy = int(hit_y + (edge == 0 ? -0.5f : 0.5f));
      //   gx = floor(hit_x);
      //   gy = floor(hit_y + (v.y > 0.0f ? 0.5f : -0.5f));
      // }

      if(!cb(hit_x, hit_y, gx, gy, edge, offset, distance)) {
        break;
      }

      // // step to the next cell: whichever boundary we hit first
      // if (ray_length_x < ray_length_y) {
      //   ix += step_x;
      //   ix = round(ix);
      //   ray_length_x += t_delta_x; // next vertical boundary
      // } else {
      //   iy += step_y;
      //   iy = round(iy);
      //   ray_length_y += t_delta_y; // next horizontal boundary
      // }
    }
  }

}