#pragma once

#include <vector>
#include "picovector.hpp"
#include "matrix.hpp"
#include "rect.hpp"
#include "point.hpp"

namespace picovector {  

  class path {
  public:
    std::vector<point, PV_STD_ALLOCATOR<point>> points;

    path(int point_count = 0);
    void add_point(const point &point);
    void add_point(float x, float y);
    void edge_points(int edge, point &s, point &e); 
    void offset_edge(point &s, point &e, float offset);
    void stroke(float offset);
    void inflate(float offset);
  };

  class shape {
  public:
    std::vector<path, PV_STD_ALLOCATOR<path>> paths;
    mat3 transform;

    shape(int path_count = 0);
    ~shape() {      
      //debug_printf("shape destructed\n");
    }
    void add_path(path path);
    rect bounds();
    /*void draw(image &img); // methods should be on image perhaps? with style/brush and transform passed in?*/
    void stroke(float thickness);
  };

}