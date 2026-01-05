#include <algorithm>
#include "algorithms.hpp"

namespace picovector {

  void dda(point_t p, point_t v, dda_callback_t cb) {

    // get the current cell of the player (and the current cell of the ray) from their position
    int playerSquareX = floor(p.x);
    int playerSquareY = floor(p.y);

    int gx = playerSquareX;
    int gy = playerSquareY;

    // change the ray angle into vectors
    // rayDirX = math.cos(rayAngle)
    // rayDirY = math.sin(rayAngle)

    // establish how many x the ray travels per y, and vice versa
    const float eps = 1e-30f;
    float angleScaleFactorX = fabs(v.x) > eps ? 1.0f / v.x : 1e30f;
    float angleScaleFactorY = fabs(v.y) > eps ? 1.0f / v.y : 1e30f;

    float rayLengthX = 0;
    float rayLengthY = 0;

    float stepX = 1;
    float stepY = 1;

    // establish the +- xy direction of the ray, and take the first step to the first x and y gridlines
    if(v.x < 0) {
      stepX = -1;
      rayLengthX = (p.x - gx) * angleScaleFactorX;
    }
    else {
      stepX = 1;
      rayLengthX = ((gx + 1) - p.x) * angleScaleFactorX;
    }

    if(v.y < 0) {
      stepY = -1;
      rayLengthY = (p.y - gy) * angleScaleFactorY;
    }
    else {
      stepY = 1;
      rayLengthY = ((gy + 1) - p.y) * angleScaleFactorY;
    }

    bool stopcast = false;
    float working_distance = 0;
    int edge = 0;

    int i = 0;
    while(true) {
      bool vertical = false;

      // check if the working_distance to the nearest gridline is shorter in x or y,
      // then use the shorter to populate the intersection orientation.
      // this happens every step regardless of whether it hits something or not
      if(rayLengthX < rayLengthY) {
        gx += stepX;
        working_distance = rayLengthX;
        rayLengthX += angleScaleFactorX;
      }
      else {
        gy += stepY;
        working_distance = rayLengthY;
        rayLengthY += angleScaleFactorY;
        vertical = true;
      }

      if(vertical) {
        edge = (stepX == 1) ? 1 : 3; // step is used to determine if the ray is travelling in the +x or -x direction
      }
      else {
        edge = (stepY == 1) ? 0 : 2;
      }

      float hit_x = p.x + (v.x * working_distance);
      float hit_y = p.y + (v.y * working_distance);

      float distance = sqrt(pow(hit_x - p.x, 2) + pow(hit_y - p.y, 2));

      // calculate the intersection offset
      float offset = vertical ? (hit_y - floor(hit_y)) : (hit_x - floor(hit_x));

      if(!cb(hit_x, hit_y, gx, gy, edge, offset, working_distance)) {
        break;
      }
    }
  }

}